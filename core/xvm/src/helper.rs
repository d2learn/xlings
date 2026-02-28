// helper
use std::io::{self, Write};
use std::fs;
use std::env;

use colored::Colorize;
use xvmlib::Workspace;

use crate::baseinfo;

// try to load local workspace, if not found, load global workspace
pub fn load_workspace() -> Workspace {
    let mut workspace;
    if fs::metadata(baseinfo::WORKSPACE_FILE).is_ok() {
        workspace = load_local_workspace();
        if !workspace.active() {
            workspace = xvmlib::get_global_workspace().clone()
        }
    } else {
        workspace = xvmlib::get_global_workspace().clone()
    }
    workspace
}

pub fn load_local_workspace() -> Workspace {
    Workspace::from(baseinfo::WORKSPACE_FILE).expect("Failed to load Workspace")
}

pub fn load_workspace_and_merge() -> Workspace {
    let mut workspace = xvmlib::get_global_workspace().clone();
    if fs::metadata(baseinfo::WORKSPACE_FILE).is_ok() {
        let local_workspace = load_local_workspace();
        if local_workspace.active() {
            if local_workspace.inherit() {
                workspace.merge(&local_workspace);
                let new_wname = local_workspace.name().to_string() + " + global";
                workspace.set_name(&new_wname);
            } else {
                workspace = local_workspace;
            }
        }
    }
    workspace
}

pub fn prompt(prompt: &str, value: &str) -> bool {

    print!("{}", prompt);

    io::stdout().flush().unwrap();

    let mut input = String::new();
    io::stdin().read_line(&mut input).unwrap();
    let input = input.trim().to_lowercase();

    input == value
}

pub fn runtime_check_and_tips() -> bool {
    let path_to_check = baseinfo::bindir();
    let path_var = env::var("PATH").unwrap_or_default();
    let separator = if cfg!(target_os = "windows") { ";" } else { ":" };
    let expected_paths = expected_runtime_bindirs(&path_to_check);

    if path_var
        .split(separator)
        .filter(|p| !p.is_empty())
        .any(|p| path_matches_expected(p, &expected_paths))
    {
        true
    } else {
        print_commands(&path_to_check);
        false
    }
}

fn expected_runtime_bindirs(path_to_check: &str) -> Vec<String> {
    let mut expected = vec![normalize_runtime_path(path_to_check)];
    if cfg!(target_os = "windows") {
        let lower = expected[0].clone();
        if let Some(prefix) = lower.strip_suffix("\\subos\\default\\bin") {
            expected.push(format!("{}\\subos\\current\\bin", prefix));
        } else if let Some(prefix) = lower.strip_suffix("\\subos\\current\\bin") {
            expected.push(format!("{}\\subos\\default\\bin", prefix));
        }
    }
    expected
}

fn path_matches_expected(path_in_env: &str, expected_paths: &[String]) -> bool {
    let normalized = normalize_runtime_path(path_in_env);
    expected_paths.iter().any(|p| p == &normalized)
}

fn normalize_runtime_path(path: &str) -> String {
    let trimmed = path.trim().trim_matches('"');
    if cfg!(target_os = "windows") {
        let mut p = trimmed.replace('/', "\\").to_ascii_lowercase();
        while p.ends_with('\\') {
            p.pop();
        }
        p
    } else {
        let mut p = trimmed.to_string();
        while p.ends_with('/') {
            p.pop();
        }
        p
    }
}

fn print_commands(path_to_check: &str) {

    println!("\n\t\t{}", "[Runtime Tips]".bold());

    if cfg!(target_os = "windows") {
        println!("\n# For PowerShell:");
        println!(
            r#"[System.Environment]::SetEnvironmentVariable("Path", "{};" + [System.Environment]::GetEnvironmentVariable("Path", "User"), "User")"#,
            path_to_check
        );

        println!("\n# For cmd:");
        println!(r#"set PATH "{};%PATH%""#, path_to_check);

        println!("\n-- {}", "run command in cmd or PowerShell.".yellow());
    } else {
        println!("\n# For bash/zsh:");
        println!(r#"export PATH="{}:$PATH""#, path_to_check);

        println!("\n# For fish:");
        println!(r#"set -Ux PATH {} $PATH"#, path_to_check);

        println!("\n-- {}", "add it to your configuration file.".yellow());
    }

    println!("\n👉 Don't forget to refresh environment variable\n")
}
