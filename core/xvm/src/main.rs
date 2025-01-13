extern crate xvmlib;

mod baseinfo;
mod helper;
mod cmdprocessor;
mod handler;

fn main() {
    //baseinfo::print_baseinfo();
    if helper::runtime_check_and_tips() {
        xvmlib::init_versiondb(baseinfo::versiondb_file().as_str());
        xvmlib::init_global_workspace(baseinfo::workspace_file().as_str());
        xvmlib::init_shims(baseinfo::bindir().as_str());
        let matches = cmdprocessor::parse_from_command_line();
        let _ = cmdprocessor::run(&matches);
    }
}
