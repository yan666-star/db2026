-- Expected final rows:
-- | 3  | 35   | three |
-- | 4  | 40   | four  |
-- | 7  | 70   | seven |
-- | 10 | 15.5 | one   |
--
-- IDs 1, 2, 5, 6 and 11 must not exist.

SELECT * FROM recovery_full ORDER BY id ASC;
SELECT * FROM recovery_full WHERE id = 10;
SELECT * FROM recovery_full WHERE id = 1;
SELECT * FROM recovery_full WHERE id = 2;
SELECT * FROM recovery_full WHERE id = 3;
SELECT * FROM recovery_full WHERE id = 4;
SELECT * FROM recovery_full WHERE id = 5;
SELECT * FROM recovery_full WHERE id = 6;
SELECT * FROM recovery_full WHERE id = 7;
SELECT * FROM recovery_full WHERE id = 11;
