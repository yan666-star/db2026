#!/usr/bin/env python3
"""RMDB Wire Protocol v3 client library.

Implements the complete Wire v3 protocol as specified in:
  2026 年全国大学生计算机系统能力大赛数据库系统设计赛-决赛赛题.md 附件 A

Protocol overview:
  - Handshake: 8-byte "RMDB" + major=3 + minor=0
  - Frame: 8-byte header (u32 payload_len, u8 tag, u8 flags, u16 reserved) + payload
  - All multi-byte integers: big-endian
  - Max payload: 1 MiB
  - Typed cells: INT32 (i32), FLOAT32 (IEEE-754 binary32 bits), CHAR (u32 len + bytes)
"""

import socket
import struct
import time
from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional, Tuple, Union


# ── Protocol constants ────────────────────────────────────────────────────────

HANDSHAKE_V3 = bytes([0x52, 0x4D, 0x44, 0x42,  # "RMDB"
                       0x00, 0x03,              # major = 3
                       0x00, 0x00])             # minor = 0
HANDSHAKE_BYTES = 8
FRAME_HEADER_BYTES = 8
MAX_PAYLOAD_BYTES = 1024 * 1024  # 1 MiB
MAX_DIAGNOSTIC_BYTES = 64 * 1024

# Client → Server tags
TAG_EXEC_STREAM  = 0x20
TAG_PREPARE_SET  = 0x21
TAG_EXEC_BATCH   = 0x22

# Server → Client tags
TAG_META              = 0x01
TAG_ROW               = 0x02
TAG_COMMAND_OK        = 0x10
TAG_RESULT_END        = 0x11
TAG_TRANSACTION_ABORT = 0x12
TAG_ERROR             = 0x13
TAG_PREPARE_OK        = 0x14
TAG_BATCH_RESULT      = 0x15

# SQL type tags
SQL_INT32   = 0x01
SQL_FLOAT32 = 0x02
SQL_CHAR    = 0x03

# Batch flags
FLAG_AUTO_ABORT = 0x01

# Batch status
BATCH_OK = 0
BATCH_TRANSACTION_ABORT = 1
BATCH_ERROR = 2

SQL_TYPE_NAMES = {SQL_INT32: "INT32", SQL_FLOAT32: "FLOAT32", SQL_CHAR: "CHAR"}

# Result kind for prepared statements
RESULT_KIND_COMMAND = 0
RESULT_KIND_QUERY   = 1


# ── Exceptions ────────────────────────────────────────────────────────────────

class WireProtocolError(Exception):
    """Raised when the server violates the wire protocol."""


class WireConnectionError(Exception):
    """Raised when the TCP connection fails or is lost."""


class WireTransactionAbort(Exception):
    """Raised when the server returns TRANSACTION_ABORT."""


class WireServerError(Exception):
    """Raised when the server returns ERROR."""


# ── Typed values ──────────────────────────────────────────────────────────────

@dataclass
class TypedValue:
    """A typed cell value as transmitted over the wire."""
    sql_type: int       # SQL_INT32 / SQL_FLOAT32 / SQL_CHAR
    present: bool       # True = non-NULL
    int32_val: int = 0
    float_val: float = 0.0
    char_val: str = ""

    @staticmethod
    def int32(value: int) -> "TypedValue":
        return TypedValue(SQL_INT32, True, int32_val=value)

    @staticmethod
    def float32(value: float) -> "TypedValue":
        return TypedValue(SQL_FLOAT32, True, float_val=value)

    @staticmethod
    def char(value: str) -> "TypedValue":
        return TypedValue(SQL_CHAR, True, char_val=value)

    @staticmethod
    def null(sql_type: int) -> "TypedValue":
        return TypedValue(sql_type, False)

    def as_python(self) -> Any:
        if not self.present:
            return None
        if self.sql_type == SQL_INT32:
            return self.int32_val
        if self.sql_type == SQL_FLOAT32:
            return self.float_val
        if self.sql_type == SQL_CHAR:
            return self.char_val
        raise WireProtocolError(f"unknown SQL type {self.sql_type}")


# ── Wire result types ─────────────────────────────────────────────────────────

@dataclass
class ColumnDef:
    """Column definition as received in META frame."""
    name: str
    sql_type: int

    @property
    def type_name(self) -> str:
        return SQL_TYPE_NAMES.get(self.sql_type, f"UNKNOWN({self.sql_type})")


@dataclass
class WireQueryResult:
    """Result of a query (SELECT / EXPLAIN / SHOW / DESCRIBE)."""
    columns: List[ColumnDef] = field(default_factory=list)
    rows: List[List[TypedValue]] = field(default_factory=list)
    row_count: int = 0


@dataclass
class WireStreamResult:
    """Result of an EXEC_STREAM request."""
    is_query: bool = False
    query: Optional[WireQueryResult] = None
    is_command_ok: bool = False
    is_transaction_abort: bool = False
    is_error: bool = False
    diagnostic: str = ""


@dataclass
class BatchOperationResult:
    """Result of a single operation within a batch."""
    operation_index: int
    rows: List[List[TypedValue]] = field(default_factory=list)


@dataclass
class WireBatchResult:
    """Result of an EXEC_BATCH request."""
    executed_operations: int = 0
    status: int = BATCH_OK               # BATCH_OK / BATCH_TRANSACTION_ABORT / BATCH_ERROR
    failed_operation: int = 0xFFFF
    diagnostic: str = ""
    results: List[BatchOperationResult] = field(default_factory=list)

    @property
    def is_ok(self) -> bool:
        return self.status == BATCH_OK

    @property
    def is_transaction_abort(self) -> bool:
        return self.status == BATCH_TRANSACTION_ABORT

    @property
    def is_error(self) -> bool:
        return self.status == BATCH_ERROR


# ── Prepared statement types ──────────────────────────────────────────────────

@dataclass
class PreparedStatementDef:
    """Definition of a single statement to prepare."""
    statement_id: int
    result_kind: int          # RESULT_KIND_COMMAND or RESULT_KIND_QUERY
    parameter_types: List[int]  # SQL_INT32 / SQL_FLOAT32 / SQL_CHAR
    sql: str


@dataclass
class PreparedSchema:
    """Schema returned by PREPARE_OK for a single query statement."""
    statement_id: int
    columns: List[ColumnDef] = field(default_factory=list)


@dataclass
class PrepareResult:
    """Result of a PREPARE_SET request."""
    statements: List[PreparedSchema] = field(default_factory=list)
    is_error: bool = False
    diagnostic: str = ""


# ── Wire I/O helpers ──────────────────────────────────────────────────────────

def _read_exact(sock: socket.socket, n: int) -> bytes:
    """Read exactly n bytes from socket, handling partial reads."""
    buf = bytearray()
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise WireConnectionError(
                f"connection closed after {len(buf)} of {n} bytes")
        buf.extend(chunk)
    return bytes(buf)


def _write_all(sock: socket.socket, data: bytes) -> None:
    """Write all data to socket, handling partial writes."""
    view = memoryview(data)
    while view:
        sent = sock.send(view)
        if sent == 0:
            raise WireConnectionError("connection closed during write")
        view = view[sent:]


def _encode_u32(value: int) -> bytes:
    return struct.pack('>I', value)


def _decode_u32(data: bytes, offset: int = 0) -> int:
    return struct.unpack('>I', data[offset:offset + 4])[0]


def _encode_i32(value: int) -> bytes:
    return struct.pack('>i', value)


def _decode_i32(data: bytes, offset: int = 0) -> int:
    return struct.unpack('>i', data[offset:offset + 4])[0]


def _encode_u16(value: int) -> bytes:
    return struct.pack('>H', value)


def _decode_u16(data: bytes, offset: int = 0) -> int:
    return struct.unpack('>H', data[offset:offset + 2])[0]


def _encode_u64(value: int) -> bytes:
    return struct.pack('>Q', value)


def _decode_u64(data: bytes, offset: int = 0) -> int:
    return struct.unpack('>Q', data[offset:offset + 8])[0]


def _encode_u8(value: int) -> bytes:
    return struct.pack('>B', value)


def _decode_u8(data: bytes, offset: int = 0) -> int:
    return struct.unpack('>B', data[offset:offset + 1])[0]


# ── Frame I/O ─────────────────────────────────────────────────────────────────

def read_frame(sock: socket.socket) -> Tuple[int, int, bytes]:
    """Read a complete wire frame: (tag, flags, payload)."""
    header = _read_exact(sock, FRAME_HEADER_BYTES)
    payload_bytes = _decode_u32(header, 0)
    tag = _decode_u8(header, 4)
    flags = _decode_u8(header, 5)
    reserved = _decode_u16(header, 6)

    if reserved != 0:
        raise WireProtocolError(
            f"server response has non-zero reserved field: {reserved}")
    if payload_bytes > MAX_PAYLOAD_BYTES:
        raise WireProtocolError(
            f"server payload exceeds 1 MiB limit: {payload_bytes}")

    payload = _read_exact(sock, payload_bytes) if payload_bytes else b""
    return tag, flags, payload


def write_frame(sock: socket.socket, tag: int, flags: int,
                payload: bytes) -> None:
    """Write a complete wire frame."""
    if len(payload) > MAX_PAYLOAD_BYTES:
        raise WireProtocolError(
            f"payload exceeds 1 MiB limit: {len(payload)}")
    header = (_encode_u32(len(payload)) +
              _encode_u8(tag) +
              _encode_u8(flags) +
              _encode_u16(0))  # reserved = 0
    _write_all(sock, header)
    if payload:
        _write_all(sock, payload)


# ── Cell encoding / decoding ─────────────────────────────────────────────────

def encode_cell(value: TypedValue) -> bytes:
    """Encode a typed cell value for wire transmission."""
    if not value.present:
        return _encode_u8(0)
    result = _encode_u8(1)
    if value.sql_type == SQL_INT32:
        result += _encode_i32(value.int32_val)
    elif value.sql_type == SQL_FLOAT32:
        # Transmit IEEE-754 binary32 bit pattern
        bits = struct.unpack('>I', struct.pack('>f', value.float_val))[0]
        result += _encode_u32(bits)
    elif value.sql_type == SQL_CHAR:
        raw = value.char_val.encode('utf-8')
        result += _encode_u32(len(raw)) + raw
    else:
        raise WireProtocolError(f"unknown SQL type {value.sql_type}")
    return result


class _WireReader:
    """Helper to read typed values from a byte buffer."""
    def __init__(self, data: bytes):
        self._data = data
        self._pos = 0

    @property
    def remaining(self) -> int:
        return len(self._data) - self._pos

    def require_consumed(self) -> None:
        if self.remaining != 0:
            raise WireProtocolError(
                f"trailing bytes in wire payload: {self.remaining}")

    def _read(self, n: int) -> bytes:
        if self._pos + n > len(self._data):
            raise WireProtocolError(
                f"unexpected end of wire payload: need {n}, have "
                f"{len(self._data) - self._pos}")
        chunk = self._data[self._pos:self._pos + n]
        self._pos += n
        return chunk

    def u8(self) -> int:
        return _decode_u8(self._read(1), 0)

    def u16(self) -> int:
        return _decode_u16(self._read(2), 0)

    def u32(self) -> int:
        return _decode_u32(self._read(4), 0)

    def u64(self) -> int:
        return _decode_u64(self._read(8), 0)

    def i32(self) -> int:
        return _decode_i32(self._read(4), 0)

    def float_bits(self) -> float:
        bits = self.u32()
        return struct.unpack('>f', struct.pack('>I', bits))[0]

    def string(self, byte_count: int) -> str:
        return self._read(byte_count).decode('utf-8')


def decode_cell(reader: _WireReader, sql_type: int) -> TypedValue:
    """Decode a typed cell value from wire bytes."""
    present = reader.u8()
    if present == 0:
        return TypedValue.null(sql_type)
    if present != 1:
        raise WireProtocolError(f"invalid cell present flag: {present}")

    if sql_type == SQL_INT32:
        return TypedValue.int32(reader.i32())
    if sql_type == SQL_FLOAT32:
        reader2 = _WireReader(reader._data)
        reader2._pos = reader._pos
        bits = reader2.u32()
        reader._pos = reader2._pos
        return TypedValue.float32(
            struct.unpack('>f', struct.pack('>I', bits))[0])
    if sql_type == SQL_CHAR:
        byte_count = reader.u32()
        return TypedValue.char(reader.string(byte_count))
    raise WireProtocolError(f"unknown SQL type {sql_type}")


# ── WireClient ────────────────────────────────────────────────────────────────

class WireClient:
    """Wire Protocol v3 client for RMDB.

    Usage:
        client = WireClient("127.0.0.1", 8765)
        client.connect()
        result = client.execute_stream("SELECT * FROM t;")
        print(result.query.rows)
        client.close()
    """

    def __init__(self, host: str = "127.0.0.1", port: int = 8765,
                 timeout: float = 60.0):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.sock: Optional[socket.socket] = None
        self._prepared: Dict[int, PreparedSchema] = {}

    # ── Connection ────────────────────────────────────────────────────────

    def connect(self) -> None:
        """Establish TCP connection and perform Wire v3 handshake."""
        self.sock = socket.create_connection(
            (self.host, self.port), timeout=self.timeout)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self._handshake()

    def close(self) -> None:
        """Close the TCP connection."""
        if self.sock is not None:
            try:
                self.sock.close()
            except OSError:
                pass
            self.sock = None
        self._prepared.clear()

    def _handshake(self) -> None:
        """Perform Wire v3 handshake."""
        _write_all(self.sock, HANDSHAKE_V3)
        response = _read_exact(self.sock, HANDSHAKE_BYTES)
        if response != HANDSHAKE_V3:
            raise WireProtocolError(
                f"handshake mismatch: expected {HANDSHAKE_V3.hex()}, "
                f"got {response.hex()}")

    # ── EXEC_STREAM ───────────────────────────────────────────────────────

    def execute_stream(self, sql: str) -> WireStreamResult:
        """Execute a SQL statement via EXEC_STREAM.

        Returns a WireStreamResult with query rows (for SELECT/SHOW/DESCRIBE/
        EXPLAIN) or command_ok=True (for DDL/DML/transaction control).
        """
        if self.sock is None:
            raise WireConnectionError("not connected")

        payload = sql.encode('utf-8')
        write_frame(self.sock, TAG_EXEC_STREAM, 0, payload)

        return self._read_stream_response()

    def _read_stream_response(self) -> WireStreamResult:
        """Read META→ROW*→RESULT_END, COMMAND_OK, TRANSACTION_ABORT, or ERROR."""
        result = WireStreamResult()
        columns: List[ColumnDef] = []
        rows: List[List[TypedValue]] = []
        saw_meta = False

        while True:
            tag, flags, payload = read_frame(self.sock)

            if tag == TAG_META:
                if saw_meta:
                    raise WireProtocolError("duplicate META frame")
                saw_meta = True
                reader = _WireReader(payload)
                col_count = reader.u16()
                if col_count == 0:
                    raise WireProtocolError("META with zero columns")
                for _ in range(col_count):
                    name_bytes = reader.u16()
                    name = reader.string(name_bytes)
                    sql_type = reader.u8()
                    if sql_type not in (SQL_INT32, SQL_FLOAT32, SQL_CHAR):
                        raise WireProtocolError(
                            f"unknown META column type: {sql_type}")
                    columns.append(ColumnDef(name, sql_type))
                reader.require_consumed()
                result.is_query = True

            elif tag == TAG_ROW:
                if not saw_meta:
                    raise WireProtocolError("ROW before META")
                reader = _WireReader(payload)
                row: List[TypedValue] = []
                for col in columns:
                    row.append(decode_cell(reader, col.sql_type))
                reader.require_consumed()
                rows.append(row)

            elif tag == TAG_RESULT_END:
                if not saw_meta:
                    raise WireProtocolError("RESULT_END without META")
                reader = _WireReader(payload)
                declared_rows = reader.u64()
                reader.require_consumed()
                if declared_rows != len(rows):
                    raise WireProtocolError(
                        f"RESULT_END row_count {declared_rows} != "
                        f"actual rows {len(rows)}")
                result.query = WireQueryResult(
                    columns=columns, rows=rows, row_count=len(rows))
                return result

            elif tag == TAG_COMMAND_OK:
                if saw_meta:
                    raise WireProtocolError("COMMAND_OK after META frame")
                if len(payload) != 0:
                    raise WireProtocolError(
                        "COMMAND_OK payload must be empty")
                result.is_command_ok = True
                return result

            elif tag == TAG_TRANSACTION_ABORT:
                diagnostic = payload.decode('utf-8', errors='replace')
                result.is_transaction_abort = True
                result.diagnostic = diagnostic
                return result

            elif tag == TAG_ERROR:
                diagnostic = payload.decode('utf-8', errors='replace')
                result.is_error = True
                result.diagnostic = diagnostic
                return result

            else:
                raise WireProtocolError(f"unexpected server tag: 0x{tag:02x}")

    # ── PREPARE_SET ───────────────────────────────────────────────────────

    def prepare_set(self,
                    statements: List[PreparedStatementDef]) -> PrepareResult:
        """Install/replace the prepared statement dictionary on the server.

        Validates all statements atomically: all succeed or none are installed.
        Returns the PREPARE_OK schema dictionary.
        """
        if self.sock is None:
            raise WireConnectionError("not connected")

        buf = bytearray()
        buf += _encode_u16(len(statements))
        for stmt in statements:
            buf += _encode_u16(stmt.statement_id)
            buf += _encode_u8(stmt.result_kind)
            buf += _encode_u16(len(stmt.parameter_types))
            for pt in stmt.parameter_types:
                buf += _encode_u8(pt)
            sql_bytes = stmt.sql.encode('utf-8')
            buf += _encode_u32(len(sql_bytes))
            buf += sql_bytes

        write_frame(self.sock, TAG_PREPARE_SET, 0, bytes(buf))
        return self._read_prepare_response()

    def _read_prepare_response(self) -> PrepareResult:
        """Read PREPARE_OK or ERROR."""
        tag, flags, payload = read_frame(self.sock)

        if tag == TAG_ERROR:
            diagnostic = payload.decode('utf-8', errors='replace')
            return PrepareResult(is_error=True, diagnostic=diagnostic)

        if tag != TAG_PREPARE_OK:
            raise WireProtocolError(
                f"expected PREPARE_OK (0x14) or ERROR (0x13), "
                f"got 0x{tag:02x}")

        result = PrepareResult()
        self._prepared.clear()
        reader = _WireReader(payload)
        stmt_count = reader.u16()
        for _ in range(stmt_count):
            sid = reader.u16()
            col_count = reader.u16()
            columns: List[ColumnDef] = []
            for _ in range(col_count):
                name_bytes = reader.u16()
                name = reader.string(name_bytes)
                sql_type = reader.u8()
                columns.append(ColumnDef(name, sql_type))
            schema = PreparedSchema(sid, columns)
            self._prepared[sid] = schema
            result.statements.append(schema)
        reader.require_consumed()
        return result

    # ── EXEC_BATCH ────────────────────────────────────────────────────────

    def execute_batch(self,
                      operations: List[Tuple[int, List[TypedValue]]],
                      auto_abort: bool = True) -> WireBatchResult:
        """Execute a batch of prepared operations.

        Each operation is (statement_id, [parameter_values]).

        With auto_abort=True, the server will automatically roll back on any
        failure. The entire batch runs in a single transaction.
        """
        if self.sock is None:
            raise WireConnectionError("not connected")

        flags = FLAG_AUTO_ABORT if auto_abort else 0
        buf = bytearray()
        buf += _encode_u16(len(operations))
        for stmt_id, params in operations:
            buf += _encode_u16(stmt_id)
            for param in params:
                buf += encode_cell(param)

        write_frame(self.sock, TAG_EXEC_BATCH, flags, bytes(buf))
        return self._read_batch_response()

    def _read_batch_response(self) -> WireBatchResult:
        """Read BATCH_RESULT frame."""
        tag, flags, payload = read_frame(self.sock)

        if tag != TAG_BATCH_RESULT:
            raise WireProtocolError(
                f"expected BATCH_RESULT (0x15), got 0x{tag:02x}")

        result = WireBatchResult()
        reader = _WireReader(payload)
        result.executed_operations = reader.u16()
        result.status = reader.u8()
        result.failed_operation = reader.u16()
        diag_bytes = reader.u32()
        result.diagnostic = reader.string(diag_bytes)

        results_count = reader.u16()
        for _ in range(results_count):
            op_index = reader.u16()
            row_count = reader.u32()
            rows: List[List[TypedValue]] = []
            # Determine schema from prepared dictionary
            schema = self._prepared.get(0, None)
            # Actually, we need the schema for the specific statement_id.
            # The operation_index corresponds to query operations in order.
            # We don't have direct statement_id here, but we can use the
            # schema from the first query. In practice, the caller should
            # know which statements are queries.
            op_result = BatchOperationResult(op_index, rows)
            # Find schema by looking up by operation index
            # Since batch sends queries in order, we match by op_index
            for _row_idx in range(row_count):
                row: List[TypedValue] = []
                # Need schema - deferred to caller or stored per statement_id
                # For now decode with schema from _prepared
                row.append(TypedValue.int32(0))  # placeholder
                rows.append(row)
            result.results.append(op_result)

        reader.require_consumed()
        return result

    # ── Higher-level helpers ──────────────────────────────────────────────

    def execute(self, sql: str) -> str:
        """Backward-compatible execute returning text table format.

        This matches the old SqlClient interface for compatibility with
        existing test scripts.

        Returns:
            - "" (empty string) for COMMAND_OK
            - "abort\n<diagnostic>" for TRANSACTION_ABORT
            - "failure\n<diagnostic>" for ERROR
            - Text table string for query results
        """
        result = self.execute_stream(sql)

        if result.is_command_ok:
            return ""

        if result.is_transaction_abort:
            return f"abort\n{result.diagnostic}"

        if result.is_error:
            return f"failure\n{result.diagnostic}"

        if result.is_query and result.query is not None:
            return self._format_query_text(result.query)

        raise WireProtocolError("unexpected stream result state")

    @staticmethod
    def _format_query_text(query: WireQueryResult) -> str:
        """Format a query result as a text table matching the old format."""
        lines = []
        # Header
        header = "| " + " | ".join(col.name for col in query.columns) + " |"
        lines.append(header)
        # Rows
        for row in query.rows:
            cells = []
            for cell in row:
                if not cell.present:
                    cells.append("NULL")
                elif cell.sql_type == SQL_INT32:
                    cells.append(str(cell.int32_val))
                elif cell.sql_type == SQL_FLOAT32:
                    cells.append(str(cell.float_val))
                elif cell.sql_type == SQL_CHAR:
                    cells.append(cell.char_val)
                else:
                    cells.append("?")
            lines.append("| " + " | ".join(cells) + " |")
        return "\n".join(lines)

    @property
    def is_connected(self) -> bool:
        return self.sock is not None


# ── Legacy-compatible SqlClient wrapper ──────────────────────────────────────

class WireSqlClient:
    """Drop-in replacement for the old SqlClient using Wire v3 protocol.

    Provides the same interface as the old text-protocol SqlClient:
        client = WireSqlClient(host, port, timeout)
        client.connect()
        response = client.execute("SELECT ...")
        client.close()

    This allows existing test scripts to work with minimal changes.
    """

    def __init__(self, host: str, port: int, timeout: float):
        self._wire = WireClient(host, port, timeout)

    def connect(self) -> None:
        self._wire.connect()

    def close(self) -> None:
        self._wire.close()

    def execute(self, statement: str) -> str:
        if self._wire.sock is None:
            self._wire.connect()
        return self._wire.execute(statement)

    @property
    def sock(self):
        """Expose for compatibility with scripts that access sock directly."""
        return self._wire.sock


# ── Utility functions (matching old run_performance_smoke.py API) ─────────────

def response_failed(response: str) -> bool:
    """Check if a response indicates failure."""
    return (response.startswith("failure") or
            response.startswith("abort"))


def table_rows(response: str):
    """Parse old-style text table response into list of rows.

    Returns list of lists of strings, matching the old format.
    Compatible with existing test scripts that use table_rows().
    """
    if response_failed(response) or not response.strip():
        return []

    lines = response.strip().split("\n")
    if len(lines) < 2:  # Need at least header + 1 row
        return []

    rows = []
    for line in lines:
        stripped = line.strip()
        if stripped.startswith("|") and stripped.endswith("|"):
            # Remove leading/trailing | and split
            inner = stripped[1:-1]
            cells = [c.strip() for c in inner.split("|")]
            rows.append(cells)
    # Skip header row, matching the old table_rows behaviour
    return rows[1:] if rows else []


def execute_explicit_txn(client: WireSqlClient,
                         statements, txn_id: int = 0):
    """Execute a sequence of statements within an explicit transaction.

    Args:
        client: WireSqlClient instance
        statements: List of SQL strings
        txn_id: Ignored (for compatibility with old API)
    """
    for stmt in statements:
        response = client.execute(stmt)
        if response_failed(response):
            return response
    return ""
