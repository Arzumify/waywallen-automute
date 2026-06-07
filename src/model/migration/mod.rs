use sea_orm_migration::prelude::*;

mod m20260503_000001_init_v2;
mod m20260523_000001_user_property_overrides;
mod m20260601_000001_playlists;

pub struct Migrator;

#[async_trait::async_trait]
impl MigratorTrait for Migrator {
    fn migrations() -> Vec<Box<dyn MigrationTrait>> {
        vec![
            Box::new(m20260503_000001_init_v2::Migration),
            Box::new(m20260523_000001_user_property_overrides::Migration),
            Box::new(m20260601_000001_playlists::Migration),
        ]
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use sea_orm::{ConnectionTrait, Statement};

    #[tokio::test]
    async fn playlist_tables_exist_after_migration() {
        let db = crate::model::connect_url("sqlite::memory:").await.unwrap();
        for table in ["playlist", "playlist_item"] {
            let stmt = Statement::from_string(
                db.get_database_backend(),
                format!("SELECT count(*) FROM {table}"),
            );
            db.execute(stmt)
                .await
                .unwrap_or_else(|e| panic!("table {table} missing: {e}"));
        }
    }
}
