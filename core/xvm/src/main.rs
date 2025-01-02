extern crate xvmlib;

//use std::env;

use xvmlib::shims;
use xvmlib::config;

fn main() {

    let target = "python";
    //let current_dir = env::current_dir().unwrap();

    let version_db = config::VersionDB::new("config/versions.xvm.yaml").unwrap();
    let wconfig = config::WorkspaceConfig::new("config/workspace.xvm.yaml").unwrap();

    let version = wconfig.get_version(target).unwrap();
    let vdata = version_db.get_vdata(target, version).unwrap();

    wconfig.active();

    let mut program = shims::Program::new(target, version);
    program.set_vdata(vdata);
    program.add_arg("--version");
    program.run();
}