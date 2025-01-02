use std::fs;
use std::io;
use std::collections::HashMap;

use serde::Deserialize;
use serde::de::DeserializeOwned;
use serde_yaml;

fn load_from_file<T>(yaml_file: &str) -> Result<T, io::Error>
where
    T: DeserializeOwned,
{
    let contents = fs::read_to_string(yaml_file)?;

    let data = serde_yaml::from_str(&contents)
        .map_err(|err| io::Error::new(io::ErrorKind::InvalidData, err))?;

    Ok(data)
}

#[derive(Debug, Deserialize)]
pub struct VData {
    pub name: Option<String>,
    pub path: String,
    pub envs: Option<HashMap<String, String>>,
}

type VersionRoot = HashMap<String, HashMap<String, VData>>;

pub struct VersionDB {
    root: VersionRoot,
}

pub struct WorkspaceConfig {
    root: HashMap<String, String>,
}

impl VersionDB {
    pub fn new(yaml_file: &str) -> Result<Self, io::Error> {
        let root = load_from_file(yaml_file)?;

        Ok(VersionDB { root })
    }

    pub fn get_vdata(&self, name: &str, version: &str) -> Option<&VData> {
        self.root.get(name)?.get(version)
    }
}

impl WorkspaceConfig {
    pub fn new(yaml_file: &str) -> Result<Self, io::Error> {
        let root = load_from_file(yaml_file)?;

        Ok(WorkspaceConfig { root })
    }

    pub fn get_version(&self, name: &str) -> Option<&String> {
        self.root.get(name)
    }

    pub fn active(&self) {
        self.root.get("xvm-workspace").map(|v| println!("Workspace: {}", v));
    }
}