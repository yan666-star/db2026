CREATE TABLE checkpoint_smoke (
    id INT,
    value INT
);

INSERT INTO checkpoint_smoke VALUES (1, 100);

CREATE STATIC_CHECKPOINT;

SELECT * FROM checkpoint_smoke;
