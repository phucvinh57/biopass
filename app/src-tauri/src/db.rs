use rusqlite::{params, Connection, OptionalExtension};
use rusqlite_migration::{Migrations, M};
use serde::{Deserialize, Serialize};
use std::time::{Duration, SystemTime, UNIX_EPOCH};
use tauri::AppHandle;

use crate::paths::{
    ensure_private_dir, ensure_private_file, get_config_dir, get_data_dir, get_db_path,
};

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct Model {
    pub id: String,
    pub name: String,
    pub task: String,
    pub architecture: Option<String>,
    pub path: String,
    pub version: Option<String>,
    pub checksum: Option<String>,
    pub source: String,
    pub installed_at: i64,
}

pub struct KnownModel {
    pub slug: &'static str,
    pub filename: &'static str,
    pub display_name: &'static str,
    pub task: &'static str,
    pub architecture: Option<&'static str>,
    /// Hex SHA-256 and byte size of the release asset: the Git LFS `oid`
    /// and `size` of auth/face/models/<filename>. Update them together.
    pub sha256: &'static str,
    pub size: u64,
}

/// Built-in models. The app downloads any that are missing on startup
/// (see models::ensure_default_models).
pub const KNOWN_MODELS: &[KnownModel] = &[
    KnownModel {
        slug: "yolov8n-face",
        filename: "yolov8n-face.onnx",
        display_name: "YOLOv8n Face",
        task: "detection",
        architecture: None,
        sha256: "3e81709ceaae911db2dd23bed39aa1fc980b6320e8a8bbf8b587b2a8feaa5a5a",
        size: 12667296,
    },
    KnownModel {
        slug: "edgeface-s-gamma-05",
        filename: "edgeface_s_gamma_05.onnx",
        display_name: "EdgeFace S (gamma 0.5)",
        task: "recognition",
        architecture: None,
        sha256: "cdc2ced431b4bf071c92f2fe8bb6ed4b5017be4175b4375eaa6e14b255f831ce",
        size: 14847458,
    },
    KnownModel {
        slug: "edgeface-xs-gamma-06",
        filename: "edgeface_xs_gamma_06.onnx",
        display_name: "EdgeFace XS (gamma 0.6)",
        task: "recognition",
        architecture: None,
        sha256: "619ffe6f9df592e250e900ed135c2de3d620f6bc0a913f7d0432fa0735073a53",
        size: 7319208,
    },
    KnownModel {
        slug: "mobilenetv3-antispoof",
        filename: "mobilenetv3_antispoof.onnx",
        display_name: "MobileNetV3 Anti-Spoof",
        task: "anti_spoofing",
        architecture: Some("mobilenetv3"),
        sha256: "a90d32b1f217658d1eb1ea05d9248d07fbd05c8ee41b4baa4c36b5cde30d40f4",
        size: 12152984,
    },
    KnownModel {
        slug: "minifas-v2",
        filename: "minifas_v2.onnx",
        display_name: "MiniFASNet V2",
        task: "anti_spoofing",
        architecture: Some("minifasv2"),
        sha256: "af2381b88f38769222ed93379e12444e2a50814575de1c46170de570c55a42b6",
        size: 1912594,
    },
];

fn now_unix() -> i64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs() as i64
}

fn migrations() -> Migrations<'static> {
    Migrations::new(vec![
        M::up(include_str!("../migrations/001_initial.sql")),
        M::up(include_str!("../migrations/002_task_and_architecture.sql")),
    ])
}

/// Opens biopass.db, runs pending migrations, and seeds built-in models.
pub fn open(app: &AppHandle) -> Result<Connection, String> {
    let config_dir = get_config_dir(app)?;
    ensure_private_dir(&config_dir)
        .map_err(|e| format!("Failed to create config directory: {}", e))?;

    // Create it 0600 before SQLite does (it would use the umask).
    let db_path = get_db_path(app)?;
    ensure_private_file(&db_path).map_err(|e| format!("Failed to create database file: {}", e))?;
    let mut conn =
        Connection::open(&db_path).map_err(|e| format!("Failed to open database: {}", e))?;
    conn.busy_timeout(Duration::from_secs(2))
        .map_err(|e| format!("Failed to set database busy timeout: {}", e))?;

    migrations()
        .to_latest(&mut conn)
        .map_err(|e| format!("Failed to run database migrations: {}", e))?;

    seed_default_models(&conn, app)?;

    Ok(conn)
}

fn seed_default_models(conn: &Connection, app: &AppHandle) -> Result<(), String> {
    let models_dir = get_data_dir(app)?.join("models");
    let now = now_unix();

    for known in KNOWN_MODELS {
        let path = models_dir.join(known.filename);
        // A 0-byte file is a failed download, not a model.
        if !std::fs::metadata(&path).is_ok_and(|m| m.is_file() && m.len() > 0) {
            continue;
        }
        conn.execute(
            "INSERT OR IGNORE INTO models (id, name, task, architecture, path, version, checksum, source, installed_at) \
             VALUES (?1, ?2, ?3, ?4, ?5, NULL, NULL, 'builtin', ?6)",
            params![
                known.slug,
                known.display_name,
                known.task,
                known.architecture,
                path.to_string_lossy(),
                now
            ],
        )
        .map_err(|e| format!("Failed to seed model '{}': {}", known.slug, e))?;
    }

    Ok(())
}

fn row_to_model(row: &rusqlite::Row) -> rusqlite::Result<Model> {
    Ok(Model {
        id: row.get(0)?,
        name: row.get(1)?,
        task: row.get(2)?,
        architecture: row.get(3)?,
        path: row.get(4)?,
        version: row.get(5)?,
        checksum: row.get(6)?,
        source: row.get(7)?,
        installed_at: row.get(8)?,
    })
}

const MODEL_COLUMNS: &str =
    "id, name, task, architecture, path, version, checksum, source, installed_at";

pub fn list_models(conn: &Connection, task: Option<&str>) -> Result<Vec<Model>, String> {
    let query = format!(
        "SELECT {} FROM models WHERE (?1 IS NULL OR task = ?1) ORDER BY installed_at",
        MODEL_COLUMNS
    );
    let mut stmt = conn
        .prepare(&query)
        .map_err(|e| format!("Failed to prepare models query: {}", e))?;
    let rows = stmt
        .query_map(params![task], row_to_model)
        .map_err(|e| format!("Failed to query models: {}", e))?;
    rows.collect::<Result<Vec<_>, _>>()
        .map_err(|e| format!("Failed to read models: {}", e))
}

pub fn get_model(conn: &Connection, id: &str) -> Result<Option<Model>, String> {
    let query = format!("SELECT {} FROM models WHERE id = ?1", MODEL_COLUMNS);
    conn.query_row(&query, params![id], row_to_model)
        .optional()
        .map_err(|e| format!("Failed to query model '{}': {}", id, e))
}

/// True when a user model already holds `id`. Old versions could hand a
/// built-in's slug to a user model, whose file then sits at the built-in's
/// path; the built-in download must not overwrite it.
pub fn is_user_model(conn: &Connection, id: &str) -> Result<bool, String> {
    Ok(get_model(conn, id)?.is_some_and(|m| m.source == "user"))
}

pub fn resolve_model_path(conn: &Connection, id: &str) -> Result<Option<String>, String> {
    conn.query_row(
        "SELECT path FROM models WHERE id = ?1",
        params![id],
        |row| row.get(0),
    )
    .optional()
    .map_err(|e| format!("Failed to resolve model path for '{}': {}", id, e))
}

pub fn delete_model(conn: &Connection, id: &str) -> Result<(), String> {
    conn.execute("DELETE FROM models WHERE id = ?1", params![id])
        .map_err(|e| format!("Failed to delete model '{}': {}", id, e))?;
    Ok(())
}

pub fn update_model_name(conn: &Connection, id: &str, name: &str) -> Result<(), String> {
    let affected = conn
        .execute(
            "UPDATE models SET name = ?2 WHERE id = ?1",
            params![id, name],
        )
        .map_err(|e| format!("Failed to rename model '{}': {}", id, e))?;
    if affected == 0 {
        return Err(format!("Model '{}' not found", id));
    }
    Ok(())
}

pub struct NewModel<'a> {
    pub name: &'a str,
    pub task: &'a str,
    pub architecture: Option<&'a str>,
    pub checksum: Option<&'a str>,
    pub source: &'a str,
}

/// Inserts `model` under the first free id of `base`, `base-2`, ... and
/// returns that id; `path_for(id)` gives the row's path.
///
/// Atomic: there is no separate "is it free?" read. A plain INSERT either
/// claims the id or hits the PRIMARY KEY, so two concurrent adds can never
/// share an id (or the `<id>.onnx` file). Built-in slugs are skipped even
/// before the built-in is seeded, so a user model never takes its file.
pub fn insert_with_unique_id(
    conn: &Connection,
    base: &str,
    model: &NewModel,
    path_for: impl Fn(&str) -> String,
) -> Result<String, String> {
    for n in 1..1000 {
        let candidate = if n == 1 {
            base.to_string()
        } else {
            format!("{base}-{n}")
        };
        if KNOWN_MODELS.iter().any(|k| k.slug == candidate) {
            continue;
        }
        let inserted = conn.execute(
            "INSERT INTO models (id, name, task, architecture, path, version, checksum, source, installed_at) \
             VALUES (?1, ?2, ?3, ?4, ?5, NULL, ?6, ?7, ?8)",
            params![
                candidate,
                model.name,
                model.task,
                model.architecture,
                path_for(&candidate),
                model.checksum,
                model.source,
                now_unix()
            ],
        );
        match inserted {
            Ok(_) => return Ok(candidate),
            Err(rusqlite::Error::SqliteFailure(e, _))
                if e.extended_code == rusqlite::ffi::SQLITE_CONSTRAINT_PRIMARYKEY => {}
            Err(e) => return Err(format!("Failed to insert model '{}': {}", candidate, e)),
        }
    }
    Err("Failed to allocate a unique model id".to_string())
}

#[cfg(test)]
mod tests {
    use super::*;
    use rusqlite::TransactionBehavior;

    #[test]
    fn migration_002_backfills_architecture() {
        let mut conn = Connection::open_in_memory().unwrap();
        migrations().to_version(&mut conn, 1).unwrap();
        conn.execute_batch(
            "INSERT INTO models (id, name, model_type, path, installed_at) VALUES \
                 ('yolov8n-face', 'YOLO', 'detection', '/m/yolo.onnx', 0), \
                 ('minifas-v2', 'MiniFAS', 'anti_spoofing', '/m/minifas_v2.onnx', 0), \
                 ('my-minifas', 'Custom', 'anti_spoofing', '/m/my-minifas.onnx', 0);",
        )
        .unwrap();

        migrations().to_latest(&mut conn).unwrap();

        let arch = |id: &str| get_model(&conn, id).unwrap().unwrap().architecture;
        assert_eq!(arch("yolov8n-face"), None);
        assert_eq!(arch("minifas-v2").as_deref(), Some("minifasv2"));
        // Custom rows keep the behaviour they had before: MobileNetV3.
        assert_eq!(arch("my-minifas").as_deref(), Some("mobilenetv3"));
    }

    #[test]
    fn anti_spoofing_rows_require_architecture() {
        let mut conn = Connection::open_in_memory().unwrap();
        migrations().to_latest(&mut conn).unwrap();

        let insert = |task: &str, arch: Option<&str>| {
            let model = NewModel {
                task,
                architecture: arch,
                ..user_model("m")
            };
            insert_with_unique_id(&conn, "m", &model, path_for)
        };
        assert!(insert("anti_spoofing", None).is_err());
        assert!(insert("anti_spoofing", Some("resnet")).is_err());
        assert!(insert("detection", Some("minifasv2")).is_err());
        assert!(insert("anti_spoofing", Some("minifasv2")).is_ok());
        assert!(insert("detection", None).is_ok());
    }

    fn user_model<'a>(name: &'a str) -> NewModel<'a> {
        NewModel {
            name,
            task: "detection",
            architecture: None,
            checksum: None,
            source: "user",
        }
    }

    fn path_for(id: &str) -> String {
        format!("/m/{id}.onnx")
    }

    #[test]
    fn unique_id_takes_next_free_suffix_without_overwriting() {
        let mut conn = Connection::open_in_memory().unwrap();
        migrations().to_latest(&mut conn).unwrap();

        let a = insert_with_unique_id(&conn, "face", &user_model("A"), path_for).unwrap();
        let b = insert_with_unique_id(&conn, "face", &user_model("B"), path_for).unwrap();
        let c = insert_with_unique_id(&conn, "face", &user_model("C"), path_for).unwrap();
        assert_eq!(
            (a.as_str(), b.as_str(), c.as_str()),
            ("face", "face-2", "face-3")
        );
        assert_eq!(get_model(&conn, "face").unwrap().unwrap().name, "A");
        assert_eq!(
            get_model(&conn, "face-2").unwrap().unwrap().path,
            "/m/face-2.onnx"
        );
    }

    #[test]
    fn unique_id_never_takes_a_builtin_slug() {
        let mut conn = Connection::open_in_memory().unwrap();
        migrations().to_latest(&mut conn).unwrap();

        // The built-in is not seeded yet (its file is not downloaded).
        let id =
            insert_with_unique_id(&conn, "yolov8n-face", &user_model("Mine"), path_for).unwrap();
        assert_eq!(id, "yolov8n-face-2");
    }

    #[test]
    fn unique_id_reports_non_conflict_errors() {
        let mut conn = Connection::open_in_memory().unwrap();
        migrations().to_latest(&mut conn).unwrap();

        let bad = NewModel {
            task: "anti_spoofing",
            ..user_model("No arch")
        };
        assert!(insert_with_unique_id(&conn, "x", &bad, path_for).is_err());
        assert!(get_model(&conn, "x").unwrap().is_none());
    }

    #[test]
    fn concurrent_adds_get_distinct_ids() {
        const ADDERS: usize = 8;
        let dir = tempfile::tempdir().unwrap();
        let db_path = dir.path().join("biopass.db");
        migrations()
            .to_latest(&mut Connection::open(&db_path).unwrap())
            .unwrap();

        let barrier = std::sync::Barrier::new(ADDERS);
        let ids: Vec<String> = std::thread::scope(|s| {
            let handles: Vec<_> = (0..ADDERS)
                .map(|i| {
                    let (db_path, barrier) = (&db_path, &barrier);
                    s.spawn(move || {
                        let conn = Connection::open(db_path).unwrap();
                        conn.busy_timeout(Duration::from_secs(10)).unwrap();
                        let name = format!("adder {i}");
                        barrier.wait();
                        let mut conn = conn;
                        let tx = conn
                            .transaction_with_behavior(TransactionBehavior::Immediate)
                            .unwrap();
                        let id = insert_with_unique_id(&tx, "face", &user_model(&name), path_for)
                            .unwrap();
                        // Widen the window between allocation and commit.
                        std::thread::sleep(Duration::from_millis(20));
                        tx.commit().unwrap();
                        id
                    })
                })
                .collect();
            handles.into_iter().map(|h| h.join().unwrap()).collect()
        });

        let mut unique = ids.clone();
        unique.sort();
        unique.dedup();
        assert_eq!(unique.len(), ADDERS, "duplicate ids: {ids:?}");
        let conn = Connection::open(&db_path).unwrap();
        assert_eq!(list_models(&conn, None).unwrap().len(), ADDERS);
    }

    #[test]
    fn is_user_model_only_for_user_rows() {
        let mut conn = Connection::open_in_memory().unwrap();
        migrations().to_latest(&mut conn).unwrap();
        conn.execute_batch(
            "INSERT INTO models (id, name, task, path, source, installed_at) VALUES \
                 ('yolov8n-face', 'Mine', 'detection', '/m/yolov8n-face.onnx', 'user', 0), \
                 ('edgeface-s-gamma-05', 'E', 'recognition', '/m/e.onnx', 'builtin', 0);",
        )
        .unwrap();

        assert!(is_user_model(&conn, "yolov8n-face").unwrap());
        assert!(!is_user_model(&conn, "edgeface-s-gamma-05").unwrap());
        assert!(!is_user_model(&conn, "minifas-v2").unwrap());
    }
}
