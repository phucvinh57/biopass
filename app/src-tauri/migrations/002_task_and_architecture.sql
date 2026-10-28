-- `model_type` held the pipeline role, not the network, so the auth side had
-- to guess the anti-spoofing architecture from file names. Rename it to
-- `task` and store the architecture explicitly.
--
-- Existing anti-spoofing rows other than the MiniFASv2 builtin get
-- 'mobilenetv3': that is how the auth side has always run them.
CREATE TABLE models_new (
    id           TEXT PRIMARY KEY,
    name         TEXT NOT NULL,
    task         TEXT NOT NULL CHECK (task IN ('detection', 'recognition', 'anti_spoofing')),
    architecture TEXT CHECK (
        (task = 'anti_spoofing' AND architecture IS NOT NULL
            AND architecture IN ('mobilenetv3', 'minifasv2'))
        OR (task <> 'anti_spoofing' AND architecture IS NULL)
    ),
    path         TEXT NOT NULL,
    version      TEXT,
    checksum     TEXT,
    source       TEXT NOT NULL DEFAULT 'builtin',
    installed_at INTEGER NOT NULL
);

INSERT INTO models_new (id, name, task, architecture, path, version, checksum, source, installed_at)
SELECT id, name, model_type,
       CASE
           WHEN model_type <> 'anti_spoofing' THEN NULL
           WHEN id = 'minifas-v2' THEN 'minifasv2'
           ELSE 'mobilenetv3'
       END,
       path, version, checksum, source, installed_at
FROM models;

DROP TABLE models;
ALTER TABLE models_new RENAME TO models;
CREATE INDEX idx_models_task ON models(task);
