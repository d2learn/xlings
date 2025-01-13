extern crate xvmlib;

use std::path::PathBuf;

mod baseinfo;
mod helper;
mod cmdprocessor;
mod handler;

fn main() {
    //baseinfo::print_baseinfo();
    let exec_path =  PathBuf::from(baseinfo::bindir() + "/xvm");
    let shortcut = xvmlib::desktop::ShortcutOptions {
        name: "xvm-test".to_string(),
        exec_path, // 直接使用 PathBuf 类型
        icon_path: Some(PathBuf::from(
            "/home/speak/Downloads/VSCode-win32-x64-1.96.2/resources/app/resources/win32/code_70x70.png",
        )),
        terminal: false,
        description: None,
        working_dir: None,
    };

    // 获取用户范围的快捷方式目录
    let shortcut_dir = xvmlib::desktop::shortcut_userdir();

    // 创建快捷方式
    match xvmlib::desktop::create_shortcut(shortcut, &shortcut_dir) {
        Ok(_) => println!(
            "Shortcut created successfully: {}",
            shortcut_dir.join("xvm.desktop").display()
        ),
        Err(e) => eprintln!("Failed to create shortcut: {}", e),
    }

    if helper::runtime_check_and_tips() {
        xvmlib::init_versiondb(baseinfo::versiondb_file().as_str());
        xvmlib::init_global_workspace(baseinfo::workspace_file().as_str());
        xvmlib::init_shims(baseinfo::bindir().as_str());
        let matches = cmdprocessor::parse_from_command_line();
        let _ = cmdprocessor::run(&matches);
    }
}
