use std::fs;
use std::io;

use serde_yaml;
use serde::de::DeserializeOwned;

pub use serde::Serialize;
pub use serde::Deserialize;
pub use indexmap::IndexMap;

pub fn load_from_file<T>(yaml_file: &str) -> Result<T, io::Error>
where
    T: DeserializeOwned,
{
    let contents = fs::read_to_string(yaml_file)?;

    let data = serde_yaml::from_str(&contents)
        .map_err(|err| io::Error::new(io::ErrorKind::InvalidData, err))?;

    Ok(data)
}

pub fn save_to_file<T>(yaml_file: &str, data: &T) -> Result<(), io::Error>
where
    T: Serialize,
{
    let yaml = serde_yaml::to_string(data)
        .map_err(|err| io::Error::new(io::ErrorKind::InvalidData, err))?;

    fs::write(yaml_file, yaml)
}