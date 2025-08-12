use std::io;

use crate::config::*;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct VData {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub alias: Option<String>,
    pub path: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub icon: Option<String>,
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

    pub fn new(yaml_file: &str) -> Self {

        let root = IndexMap::new();

        save_to_file(yaml_file, &root).expect("Failed to save VersionDB");

        VersionDB {
            filename: yaml_file.to_string(),
            root: root,
        }
    }

    pub fn from(yaml_file: &str) -> Result<Self, io::Error> {
        let root = load_from_file(yaml_file)?;

        Ok(VersionDB {
            filename: yaml_file.to_string(),
            root: root,
        })
    }

    pub fn is_empty(&self, name: &str) -> bool {
        self.root.get(name).is_none()
    }

    pub fn has_version(&self, name: &str, version: &str) -> bool {
        self.root.get(name).map_or(false, |versions| versions.contains_key(version))
    }

    pub fn has_target(&self, name: &str) -> bool {
        self.root.contains_key(name)
    }

    pub fn get_all_version(&self, name: &str) -> Option<Vec<String>> {
        self.root.get(name).map(|versions| versions.keys().cloned().collect())
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

    // support match by name, return a vector of (name, versions-name)
    pub fn match_by(&self, name: &str) -> Vec<(String, Vec<String>)> {
        self.root
            .iter()
            .filter(|(n, _)| n.contains(name))
            .map(|(n, versions)| (n.clone(), versions.keys().cloned().collect()))
            .collect()
    }

    // get first version
    pub fn get_first_version(&self, name: &str) -> Option<&String> {
        self.root.get(name)?.keys().next()
    }

    // get first version matched by version-str
    pub fn match_first_version(&self, name: &str, version: &str) -> Option<&String> {

        let version_parts: Vec<&str> = version.split('.').collect();
    
        if let Some(versions) = self.root.get(name) {
            let mut matching_versions: Vec<&String> = versions.keys()
                .filter(|v| {
                    let target_parts: Vec<&str> = v.split('.').collect();
                    version_parts.iter().zip(target_parts.iter()).all(|(input, target)| input == target)
                })
                .collect();
    
            matching_versions.sort_by(|a, b| cmp_versions(b, a));
    
            return matching_versions.first().copied();
        }

        None
    }

    pub fn remove_vdata(&mut self, name: &str, version: &str) {
        if let Some(versions) = self.root.get_mut(name) {
            versions.remove(version);
            // Remove the target if it has no versions
            if versions.is_empty() {
                self.root.remove(name);
            }
        }
    }

    pub fn remove_all_vdata(&mut self, name: &str) {
        self.root.remove(name);
    }

    pub fn save_to_local(&self) -> Result<(), io::Error> {
        save_to_file(&self.filename, &self.root)
    }

}

fn cmp_versions(a: &str, b: &str) -> std::cmp::Ordering {
    let a_parts: Vec<u32> = a.split('.').filter_map(|x| x.parse().ok()).collect();
    let b_parts: Vec<u32> = b.split('.').filter_map(|x| x.parse().ok()).collect();

    for (a_part, b_part) in a_parts.iter().zip(b_parts.iter()) {
        match a_part.cmp(b_part) {
            std::cmp::Ordering::Equal => continue,
            other => return other,
        }
    }

    a_parts.len().cmp(&b_parts.len())
}