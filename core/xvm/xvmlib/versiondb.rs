use std::io;

use crate::config::*;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct VData {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub filename: Option<String>,
    pub path: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub envs: Option<IndexMap<String, String>>,
}

type VersionRoot = IndexMap<String, IndexMap<String, VData>>;

#[derive(Debug, Clone)]
pub struct VersionDB {
    filename: String,
    root: VersionRoot,
}

impl VersionDB {
    pub fn new(yaml_file: &str) -> Result<Self, io::Error> {
        let root = load_from_file(yaml_file)?;

        Ok(VersionDB {
            filename: yaml_file.to_string(),
            root: root,
        })
    }

    pub fn get_vdata(&self, name: &str, version: &str) -> Option<&VData> {
        self.root.get(name)?.get(version)
    }

    pub fn set_vdata(&mut self, name: &str, version: &str, vdata: VData) {
        self.root
            .entry(name.to_string())
            .or_insert_with(IndexMap::new)
            .insert(version.to_string(), vdata);
    }

    pub fn save_to_local(&self) -> Result<(), io::Error> {
        save_to_file(&self.filename, &self.root)
    }

}