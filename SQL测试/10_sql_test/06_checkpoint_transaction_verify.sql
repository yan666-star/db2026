-- Expected rows after recovery:
-- | id | amount    | tag       |
-- | 1  | 10.500000 | before_cp |
-- | 2  | 20.250000 | before_cp |

SELECT * FROM recovery_probe ORDER BY id ASC;
