use std::io;

use crate::config::*;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct WMetadata {
    pub name: String,
    pub active: bool,
    pub inherit: bool,
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

    pub fn new(yaml_file: &str, name: &str) -> Self {
        let root = WorkspaceRoot {
            wmetadata: WMetadata {
                name: name.to_string(),
                active: true,
                inherit: true,
            },
            versions: IndexMap::new(),
        };

        save_to_file(yaml_file, &root).expect("Failed to save Workspace");

        Workspace {
            file: yaml_file.to_string(),
            root: root,
        }
    }

    pub fn from(yaml_file: &str) -> Result<Self, io::Error> {
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

    pub fn set_active(&mut self, active: &bool) {
        self.root.wmetadata.active = active.clone();
    }

    pub fn inherit(&self) -> bool {
        self.root.wmetadata.inherit
    }

    pub fn set_inherit(&mut self, inherit: &bool) {
        self.root.wmetadata.inherit = inherit.clone();
    }

    pub fn remove(&mut self, name: &str) {
        self.root.versions.remove(name);
    }

    pub fn merge(&mut self, other: &Workspace) {
        for (name, version) in &other.root.versions {
            self.set_version(name, version);
        }
    }

    pub fn match_by(&self, name: &str) -> Vec<(&String, &String)> {
        self.root.versions.iter().filter(|(k, _)| k.contains(name)).collect()
    }

    // return all (target, version) pairs
    pub fn all_versions(&self) -> Vec<(&String, &String)> {
        self.root.versions.iter().collect()
    }

    pub fn save_to_local(&self) -> Result<(), io::Error> {
        save_to_file(&self.file, &self.root)
    }
}