use std::io;

use crate::config::*;
use indexmap::map::Entry;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct VData {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub alias: Option<String>,
    pub path: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub envs: Option<IndexMap<String, String>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub bindings: Option<IndexMap<String, String>>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct VInfo {
    // vtype -> type avoid conflict with Rust keyword
    #[serde(skip_serializing_if = "Option::is_none", rename = "type")]
    pub vtype: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub filename: Option<String>,
    pub vdata_list: IndexMap<String, VData>,
}

type VersionRootV1 = IndexMap<String, IndexMap<String, VData>>;
type VersionRoot = IndexMap<String, VInfo>;

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

        let root: Result<VersionRoot, _> = load_from_file(yaml_file);

        let root = match root {
            Ok(root) => root,
            Err(_) => {
                // try load as old version structure
                let old_root: VersionRootV1 = load_from_file(yaml_file)
                    .map_err(|_| io::Error::new(io::ErrorKind::InvalidData, "Failed to load as VersionRoot or VersionRootV1"))?;

                // VersionRootV1 → VersionRoot
                let converted: VersionRoot = old_root
                    .into_iter()
                    .map(|(name, vdata_list)| {
                        let vinfo = VInfo {
                            vtype: None,
                            filename: None,
                            vdata_list,
                        };
                        (name, vinfo)
                    })
                    .collect();

                converted
            }
        };

        Ok(VersionDB {
            filename: yaml_file.to_string(),
            root: root,
        })
    }

    pub fn is_empty(&self, name: &str) -> bool {
        self.root.get(name).is_none()
    }

    pub fn has_version(&self, name: &str, version: &str) -> bool {
        self.root.get(name).map_or(false, |info| info.vdata_list.contains_key(version))
    }

    pub fn has_target(&self, name: &str) -> bool {
        self.root.contains_key(name)
    }

    pub fn get_all_version(&self, name: &str) -> Option<Vec<String>> {
        self.root.get(name).map(|info| info.vdata_list.keys().cloned().collect())
    }

    pub fn get_filename (&self, name: &str) -> Option<&String> {
        self.root.get(name)?.filename.as_ref()
    }

    pub fn set_filename(&mut self, name: &str, filename: &str) {
        let info = self.root.entry(name.to_string());
        
        if let Entry::Vacant(entry) = info {
            entry.insert(VInfo {
                vtype: None,
                filename: Some(filename.to_string()),
                vdata_list: IndexMap::new()
            });
        } else if let Entry::Occupied(mut entry) = info {
            // if exist old filename and isn't new filename then print info
            if let Some(old_filename) = entry.get().filename.as_ref() {
                if old_filename != filename {
                    println!("xvm-warning: changing filename of '{}' from '{}' to '{}'", name, old_filename, filename);
                }
            }
            entry.get_mut().filename = Some(filename.to_string());
        }
    }

    pub fn get_type(&self, name: &str) -> Option<&String> {
        self.root.get(name)?.vtype.as_ref()
    }

    pub fn set_type(&mut self, name: &str, vtype: &str) {
        let info = self.root.entry(name.to_string());
        
        if let Entry::Vacant(entry) = info {
            entry.insert(VInfo {
                vtype: Some(vtype.to_string()),
                filename: None,
                vdata_list: IndexMap::new()
            });
        } else if let Entry::Occupied(mut entry) = info {
            // if exist old type and isn't new type then print info
            if let Some(old_type) = entry.get().vtype.as_ref() {
                // TODO: optimize
                if old_type != vtype {
                    println!("xvm-warning: changing type of '{}' from '{}' to '{}'", name, old_type, vtype);
                }
            }
            entry.get_mut().vtype = Some(vtype.to_string());
        }
    }

    pub fn add_binding(&mut self, name: &str, version: &str, binding: (String, String)) {
        let vdata = self.root
            .entry(name.to_string())
            .or_insert_with(|| VInfo { vtype: None, filename: None, vdata_list: IndexMap::new() })
            .vdata_list
            .entry(version.to_string());
        if let Entry::Occupied(mut entry) = vdata {
            entry.get_mut().bindings
                .get_or_insert_with(IndexMap::new)
                .insert(binding.0, binding.1);
        } else {
            eprintln!("xvm-error: version '{}' of target '{}' does not exist", version, name);
        }
    }

    pub fn remove_binding(&mut self, name: &str, version: &str, binding_target: &str) {
        if let Some(info) = self.root.get_mut(name) {
            if let Some(vdata) = info.vdata_list.get_mut(version) {
                if let Some(bindings) = vdata.bindings.as_mut() {
                    bindings.remove(binding_target);
                    // Remove the bindings map if it becomes empty
                    if bindings.is_empty() {
                        vdata.bindings = None;
                    }
                }
            } else {
                eprintln!("xvm-warn: version '{}' of target '{}' does not exist", version, name);
            }
        } else {
            eprintln!("xvm-warn: target '{}' does not exist", name);
        }
    }

    pub fn get_bindings(&self, name: &str, version: &str) -> Option<&IndexMap<String, String>> {
        self.root.get(name)?.vdata_list.get(version)?.bindings.as_ref()
    }

    pub fn get_vdata(&self, name: &str, version: &str) -> Option<&VData> {
        self.root.get(name)?.vdata_list.get(version)
    }

    pub fn set_vdata(&mut self, name: &str, version: &str, vdata: VData) {
        self.root
            .entry(name.to_string())
            .or_insert_with(|| VInfo { vtype: None, filename: None, vdata_list: IndexMap::new() })
            .vdata_list
            .insert(version.to_string(), vdata);
    }

    // support match by name, return a vector of (name, versions-name)
    pub fn match_by(&self, name: &str) -> Vec<(String, Vec<String>)> {
        self.root
            .iter()
            .filter(|(n, _)| n.contains(name))
            .map(|(n, info)| (n.clone(), info.vdata_list.keys().cloned().collect()))
            .collect()
    }

    // get first version
    pub fn get_first_version(&self, name: &str) -> Option<&String> {
        self.root.get(name)?.vdata_list.keys().next()
    }

    // get first version matched by version-str
    pub fn match_first_version(&self, name: &str, version: &str) -> Option<&String> {

        let version_parts: Vec<&str> = version.split('.').collect();
    
        if let Some(info) = self.root.get(name) {
            let mut matching_versions: Vec<&String> = info.vdata_list.keys()
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
        if let Some(info) = self.root.get_mut(name) {
            info.vdata_list.remove(version);
            // Remove the target if it has no version
            if info.vdata_list.is_empty() {
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