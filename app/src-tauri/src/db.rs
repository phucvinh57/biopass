use rusqlite::{params, Connection, OptionalExtension};
use rusqlite_migration::{Migrations, M};
use serde::{Deserialize, Serialize};
use std::time::{Duration, SystemTime, UNIX_EPOCH};
use tauri::AppHandle;

use crate::paths::{get_config_dir, get_data_dir, get_db_path};

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
    },
    KnownModel {
        slug: "edgeface-s-gamma-05",
        filename: "edgeface_s_gamma_05.onnx",
        display_name: "EdgeFace S (gamma 0.5)",
        task: "recognition",
        architecture: None,
    },
    KnownModel {
        slug: "edgeface-xs-gamma-06",
        filename: "edgeface_xs_gamma_06.onnx",
        display_name: "EdgeFace XS (gamma 0.6)",
        task: "recognition",
        architecture: None,
    },
    KnownModel {
        slug: "mobilenetv3-antispoof",
        filename: "mobilenetv3_antispoof.onnx",
        display_name: "MobileNetV3 Anti-Spoof",
        task: "anti_spoofing",
        architecture: Some("mobilenetv3"),
    },
    KnownModel {
        slug: "minifas-v2",
        filename: "minifas_v2.onnx",
        display_name: "MiniFASNet V2",
        task: "anti_spoofing",
        architecture: Some("minifasv2"),
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

/// Opens (creating if necessary) the biopass.db sqlite database, applies any
/// pending migrations, and seeds the `models` table from the known bundled
/// models on first run.
pub fn open(app: &AppHandle) -> Result<Connection, String> {
    let config_dir = get_config_dir(app)?;
    if !config_dir.exists() {
        std::fs::create_dir_all(&config_dir)
            .map_err(|e| format!("Failed to create config directory: {}", e))?;
    }

    let db_path = get_db_path(app)?;
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
        if !path.exists() {
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

#[allow(clippy::too_many_arguments)]
pub fn upsert_model(
    conn: &Connection,
    id: &str,
    name: &str,
    task: &str,
    architecture: Option<&str>,
    path: &str,
    version: Option<&str>,
    checksum: Option<&str>,
    source: &str,
) -> Result<(), String> {
    conn.execute(
        "INSERT INTO models (id, name, task, architecture, path, version, checksum, source, installed_at) \
         VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9) \
         ON CONFLICT(id) DO UPDATE SET \
             name = excluded.name, task = excluded.task, architecture = excluded.architecture, \
             path = excluded.path, \
             version = excluded.version, checksum = excluded.checksum, source = excluded.source, \
             installed_at = excluded.installed_at",
        params![
            id,
            name,
            task,
            architecture,
            path,
            version,
            checksum,
            source,
            now_unix()
        ],
    )
    .map_err(|e| format!("Failed to upsert model '{}': {}", id, e))?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

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
            upsert_model(&conn, "m", "m", task, arch, "/m.onnx", None, None, "user")
        };
        assert!(insert("anti_spoofing", None).is_err());
        assert!(insert("anti_spoofing", Some("resnet")).is_err());
        assert!(insert("detection", Some("minifasv2")).is_err());
        assert!(insert("anti_spoofing", Some("minifasv2")).is_ok());
        assert!(insert("detection", None).is_ok());
    }
}
