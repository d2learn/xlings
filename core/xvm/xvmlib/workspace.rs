use std::io;

use crate::config::*;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct WMetadata {
    pub name: String,
    pub active: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct WorkspaceRoot {
    #[serde(rename = "xvm-wmetadata")]
    pub wmetadata: WMetadata,
    pub versions: IndexMap<String, String>,
}

#[derive(Debug, Clone)]
pub struct Workspace {
    file: String,
    root: WorkspaceRoot,
}

impl Workspace {
    pub fn new(yaml_file: &str) -> Result<Self, io::Error> {
        let root = load_from_file(yaml_file)?;

        Ok(Workspace {
            file: yaml_file.to_string(),
            root: root,
        })
    }

    pub fn version(&self, name: &str) -> Option<&String> {
        self.root.versions.get(name)
    }

    pub fn set_version(&mut self, name: &str, version: &str) {
        self.root.versions.insert(name.to_string(), version.to_string());
    }

    pub fn name(&self) -> &str {
        &self.root.wmetadata.name
    }

    pub fn set_name(&mut self, name: &str) {
        self.root.wmetadata.name = name.to_string();
    }

    pub fn active(&self) -> bool {
        self.root.wmetadata.active
    }

    pub fn set_active(&mut self, active: bool) {
        self.root.wmetadata.active = active;
    }

    pub fn save_to_local(&self) -> Result<(), io::Error> {
        save_to_file(&self.file, &self.root)
    }
}