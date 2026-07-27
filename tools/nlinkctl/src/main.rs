// Phy-nspire calculator deployment tool.
// Copyright (C) 2026 Phy-nspire contributors
//
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation, version 3 of the License.

use std::env;
use std::error::Error;
use std::ffi::OsString;
use std::fs;
use std::path::PathBuf;

use clap::{Args, Parser, Subcommand, ValueEnum};
use libnspire::{dir::EntryType, Handle, PID, PID_CX2, VID};
use rusb::GlobalContext;
use sha2::{Digest, Sha256};

type Result<T> = std::result::Result<T, Box<dyn Error>>;

#[derive(Debug, Parser)]
#[command(
    name = "phy-nlinkctl",
    version,
    about = "Reliable TI-Nspire CX II file transfer and Phy-nspire deployment"
)]
struct Cli {
    /// CX II CSP payload size. 1280 keeps margin below the native 1440-byte
    /// frame and completes a verified 1.1 MB usbipd deployment before the
    /// observed long-transfer failure window.
    #[arg(long, default_value_t = 1280, global = true)]
    cx2_packet_size: u32,

    /// CX II payload expected while reading files from the calculator.
    #[arg(long, default_value_t = 1440, global = true)]
    cx2_read_packet_size: u32,

    /// Maximum wait for one CX II ACK read.
    #[arg(long, default_value_t = 1500, global = true)]
    ack_timeout_ms: u32,

    /// Maximum wait for one CX II handshake or response read.
    #[arg(long, default_value_t = 5000, global = true)]
    handshake_timeout_ms: u32,

    /// Number of times to transmit a packet when its ACK is lost.
    #[arg(long, default_value_t = 4, global = true)]
    ack_retries: u32,

    #[command(subcommand)]
    command: Command,
}

#[derive(Debug, Subcommand)]
enum Command {
    /// List a calculator directory.
    Ls { remote_dir: String },
    /// Print a remote file's size.
    Stat { remote_file: String },
    /// Create a calculator directory.
    Mkdir { remote_dir: String },
    /// Move or rename a calculator file.
    Mv { source: String, destination: String },
    /// Delete a calculator file.
    Rm { remote_file: String },
    /// Delete an empty calculator directory.
    Rmdir { remote_dir: String },
    /// Upload directly to an exact remote file path.
    Upload {
        local_file: PathBuf,
        remote_file: String,
    },
    /// Download one remote file.
    Download {
        remote_file: String,
        local_file: PathBuf,
    },
    /// Safely deploy through a verified temporary file and rollback backup.
    Deploy(DeployArgs),
}

#[derive(Clone, Copy, Debug, Eq, PartialEq, ValueEnum)]
enum VerifyMode {
    Size,
    Sha256,
}

#[derive(Debug, Args)]
struct DeployArgs {
    /// Local .tns application.
    local_file: PathBuf,

    /// Exact calculator destination.
    #[arg(long, default_value = "/phy-nspire/phy-nspire.tns")]
    remote: String,

    /// Verification performed before replacing the current application.
    #[arg(long, value_enum, default_value_t = VerifyMode::Sha256)]
    verify: VerifyMode,

    /// Delete the rollback copy after a successful replacement.
    #[arg(long)]
    remove_backup: bool,

    /// Verify and promote an already uploaded .upload.tns file.
    #[arg(long)]
    reuse_temporary: bool,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
struct TransportConfig {
    packet_size: u32,
    read_packet_size: u32,
    ack_timeout_ms: u32,
    handshake_timeout_ms: u32,
    ack_retries: u32,
}

impl TransportConfig {
    fn from_cli(cli: &Cli) -> Result<Self> {
        let config = Self {
            packet_size: cli.cx2_packet_size,
            read_packet_size: cli.cx2_read_packet_size,
            ack_timeout_ms: cli.ack_timeout_ms,
            handshake_timeout_ms: cli.handshake_timeout_ms,
            ack_retries: cli.ack_retries,
        };
        config.validate()?;
        Ok(config)
    }

    fn validate(self) -> Result<()> {
        if !(64..=1440).contains(&self.packet_size) {
            return Err(format!(
                "--cx2-packet-size must be in 64..=1440, got {}",
                self.packet_size
            )
            .into());
        }
        if !(64..=1440).contains(&self.read_packet_size) {
            return Err(format!(
                "--cx2-read-packet-size must be in 64..=1440, got {}",
                self.read_packet_size
            )
            .into());
        }
        if !(50..=60_000).contains(&self.ack_timeout_ms) {
            return Err(format!(
                "--ack-timeout-ms must be in 50..=60000, got {}",
                self.ack_timeout_ms
            )
            .into());
        }
        if !(50..=60_000).contains(&self.handshake_timeout_ms) {
            return Err(format!(
                "--handshake-timeout-ms must be in 50..=60000, got {}",
                self.handshake_timeout_ms
            )
            .into());
        }
        if !(1..=20).contains(&self.ack_retries) {
            return Err(
                format!("--ack-retries must be in 1..=20, got {}", self.ack_retries).into(),
            );
        }
        Ok(())
    }

    fn apply(self) {
        // The patched, vendored libnspire transport reads these before it
        // opens the device. This keeps the public libnspire API unchanged.
        env::set_var("NSPIRE_CX2_PACKET_SIZE", self.packet_size.to_string());
        env::set_var(
            "NSPIRE_CX2_READ_PACKET_SIZE",
            self.read_packet_size.to_string(),
        );
        env::set_var("NSPIRE_CX2_ACK_TIMEOUT_MS", self.ack_timeout_ms.to_string());
        env::set_var(
            "NSPIRE_CX2_HANDSHAKE_TIMEOUT_MS",
            self.handshake_timeout_ms.to_string(),
        );
        env::set_var("NSPIRE_CX2_ACK_RETRIES", self.ack_retries.to_string());
    }
}

struct PacketSizeOverride {
    previous: Option<OsString>,
}

impl PacketSizeOverride {
    fn for_read() -> Self {
        let previous = env::var_os("NSPIRE_CX2_PACKET_SIZE");
        if let Some(read_size) = env::var_os("NSPIRE_CX2_READ_PACKET_SIZE") {
            env::set_var("NSPIRE_CX2_PACKET_SIZE", read_size);
        }
        Self { previous }
    }
}

impl Drop for PacketSizeOverride {
    fn drop(&mut self) {
        match self.previous.take() {
            Some(value) => env::set_var("NSPIRE_CX2_PACKET_SIZE", value),
            None => env::remove_var("NSPIRE_CX2_PACKET_SIZE"),
        }
    }
}

struct Progress {
    label: &'static str,
    total: usize,
    next_percent: usize,
}

impl Progress {
    fn new(label: &'static str, total: usize) -> Self {
        eprintln!("{label}: 0/{total} bytes (0%)");
        Self {
            label,
            total,
            next_percent: 10,
        }
    }

    fn update(&mut self, remaining: usize) {
        let sent = self.total.saturating_sub(remaining);
        let percent = sent
            .saturating_mul(100)
            .checked_div(self.total)
            .unwrap_or(100);
        if percent >= self.next_percent {
            eprintln!(
                "{}: {}/{} bytes ({}%)",
                self.label, sent, self.total, percent
            );
            self.next_percent = ((percent / 10) + 1).saturating_mul(10);
        }
    }

    fn finish(&mut self) {
        if self.next_percent <= 100 {
            eprintln!("{}: {}/{} bytes (100%)", self.label, self.total, self.total);
        }
        self.next_percent = 101;
    }
}

fn open_device() -> Result<Handle<GlobalContext>> {
    for device in rusb::devices()?.iter() {
        let descriptor = device.device_descriptor()?;
        if descriptor.vendor_id() == VID && matches!(descriptor.product_id(), PID | PID_CX2) {
            return Ok(Handle::new(device.open()?)?);
        }
    }
    Err("no TI-Nspire device found".into())
}

fn remote_parts(path: &str) -> Result<(String, String)> {
    if !path.starts_with('/') || path == "/" || path.ends_with('/') {
        return Err(format!("expected an absolute remote file path, got {path:?}").into());
    }
    let split = path
        .rfind('/')
        .ok_or_else(|| format!("invalid remote path {path:?}"))?;
    let parent = if split == 0 {
        "/".to_string()
    } else {
        path[..split].to_string()
    };
    let name = path[split + 1..].to_string();
    if name.is_empty() {
        return Err(format!("remote path has no file name: {path:?}").into());
    }
    Ok((parent, name))
}

fn sibling_path(path: &str, suffix: &str) -> Result<String> {
    let (parent, name) = remote_parts(path)?;
    let (stem, extension) = match name.rsplit_once('.') {
        Some((stem, extension)) if !stem.is_empty() => (stem, format!(".{extension}")),
        _ => (name.as_str(), String::new()),
    };
    let separator = if parent == "/" { "" } else { "/" };
    Ok(format!("{parent}{separator}{stem}{suffix}{extension}"))
}

fn entry_exists(handle: &Handle<GlobalContext>, path: &str, entry_type: EntryType) -> Result<bool> {
    let (parent, name) = remote_parts(path)?;
    Ok(handle
        .list_dir(&parent)?
        .iter()
        .any(|entry| entry.entry_type() == entry_type && entry.name().to_string_lossy() == name))
}

fn upload_bytes(handle: &Handle<GlobalContext>, remote: &str, data: &[u8]) -> Result<()> {
    let mut progress = Progress::new("upload", data.len());
    handle.write_file(remote, data, &mut |remaining| progress.update(remaining))?;
    progress.finish();
    Ok(())
}

fn download_bytes(handle: &Handle<GlobalContext>, remote: &str) -> Result<Vec<u8>> {
    let attr = handle.file_attr(remote)?;
    let mut data = vec![0u8; attr.size() as usize];
    let mut progress = Progress::new("download", data.len());
    // The calculator's file-read service emits its native 1440-byte CSP
    // frames. Asking data_read for the 512-byte upload-safe size truncates
    // each inbound frame and eventually waits for bytes that were discarded.
    let _packet_size = PacketSizeOverride::for_read();
    let bytes = handle.read_file(remote, &mut data, &mut |remaining| {
        progress.update(remaining)
    })?;
    progress.finish();
    if bytes != data.len() {
        return Err(format!(
            "short remote read for {remote}: expected {}, got {bytes}",
            data.len()
        )
        .into());
    }
    Ok(data)
}

fn sha256(data: &[u8]) -> String {
    format!("{:x}", Sha256::digest(data))
}

fn verify_remote(
    handle: &Handle<GlobalContext>,
    remote: &str,
    local: &[u8],
    mode: VerifyMode,
) -> Result<()> {
    let attr = handle.file_attr(remote)?;
    if attr.size() as usize != local.len() {
        return Err(format!(
            "remote size mismatch for {remote}: expected {}, got {}",
            local.len(),
            attr.size()
        )
        .into());
    }
    println!("verified-size\t{remote}\t{}", attr.size());

    if mode == VerifyMode::Sha256 {
        let remote_data = download_bytes(handle, remote)?;
        let local_hash = sha256(local);
        let remote_hash = sha256(&remote_data);
        if remote_hash != local_hash {
            return Err(format!(
                "remote SHA-256 mismatch for {remote}: local {local_hash}, remote {remote_hash}"
            )
            .into());
        }
        println!("verified-sha256\t{remote}\t{local_hash}");
    }
    Ok(())
}

fn rollback(
    handle: &Handle<GlobalContext>,
    remote: &str,
    temporary: &str,
    backup: &str,
) -> Result<()> {
    if entry_exists(handle, remote, EntryType::File)? {
        handle.move_file(remote, temporary)?;
    }
    if entry_exists(handle, backup, EntryType::File)? {
        handle.move_file(backup, remote)?;
        eprintln!("rollback restored {remote}");
        return Ok(());
    }
    Err(format!("rollback copy is missing: {backup}").into())
}

fn deploy(handle: &Handle<GlobalContext>, args: &DeployArgs) -> Result<()> {
    let local = fs::read(&args.local_file)?;
    let temporary = sibling_path(&args.remote, ".upload")?;
    let backup = sibling_path(&args.remote, ".previous")?;

    println!(
        "local\t{}\t{} bytes",
        args.local_file.display(),
        local.len()
    );
    println!("temporary\t{temporary}");
    println!("target\t{}", args.remote);
    println!("backup\t{backup}");

    if args.reuse_temporary {
        if !entry_exists(handle, &temporary, EntryType::File)? {
            return Err(format!(
                "--reuse-temporary requested, but the remote file is missing: {temporary}"
            )
            .into());
        }
        println!("reused-temporary\t{temporary}");
    } else {
        if entry_exists(handle, &temporary, EntryType::File)? {
            handle.delete_file(&temporary)?;
            println!("removed-stale-temporary\t{temporary}");
        }
        upload_bytes(handle, &temporary, &local)?;
    }

    if let Err(error) = verify_remote(handle, &temporary, &local, args.verify) {
        return Err(format!(
            "temporary upload verification failed; current application was not touched: {error}"
        )
        .into());
    }

    let target_existed = entry_exists(handle, &args.remote, EntryType::File)?;
    if entry_exists(handle, &backup, EntryType::File)? {
        handle.delete_file(&backup)?;
        println!("removed-stale-backup\t{backup}");
    }
    if target_existed {
        handle.move_file(&args.remote, &backup)?;
        println!("backed-up\t{} -> {backup}", args.remote);
    }

    if let Err(error) = handle.move_file(&temporary, &args.remote) {
        if target_existed {
            let rollback_result = rollback(handle, &args.remote, &temporary, &backup);
            return Err(format!(
                "replacement failed: {error}; rollback result: {rollback_result:?}"
            )
            .into());
        }
        return Err(format!("replacement failed: {error}").into());
    }

    if let Err(error) = verify_remote(handle, &args.remote, &local, VerifyMode::Size) {
        if target_existed {
            let rollback_result = rollback(handle, &args.remote, &temporary, &backup);
            return Err(format!(
                "post-replacement verification failed: {error}; rollback result: {rollback_result:?}"
            )
            .into());
        }
        return Err(format!("post-replacement verification failed: {error}").into());
    }

    if args.remove_backup && target_existed {
        handle.delete_file(&backup)?;
        println!("removed-backup\t{backup}");
    } else if target_existed {
        println!("kept-rollback\t{backup}");
    }
    println!("deployed\t{}\t{} bytes", args.remote, local.len());
    Ok(())
}

fn run() -> Result<()> {
    let cli = Cli::parse();
    let transport = TransportConfig::from_cli(&cli)?;
    transport.apply();
    eprintln!(
        "CX II transport: write packet={} bytes, read packet={} bytes, ACK timeout={} ms, handshake timeout={} ms, attempts={}",
        transport.packet_size,
        transport.read_packet_size,
        transport.ack_timeout_ms,
        transport.handshake_timeout_ms,
        transport.ack_retries
    );

    let handle = open_device()?;
    match &cli.command {
        Command::Ls { remote_dir } => {
            for entry in handle.list_dir(remote_dir)?.iter() {
                let suffix = if entry.entry_type() == EntryType::Directory {
                    "/"
                } else {
                    ""
                };
                println!(
                    "{}{}\t{}",
                    entry.name().to_string_lossy(),
                    suffix,
                    entry.size()
                );
            }
        }
        Command::Stat { remote_file } => {
            let attr = handle.file_attr(remote_file)?;
            println!("{remote_file}\t{}", attr.size());
        }
        Command::Mkdir { remote_dir } => {
            handle.create_dir(remote_dir)?;
            println!("created\t{remote_dir}");
        }
        Command::Mv {
            source,
            destination,
        } => {
            handle.move_file(source, destination)?;
            println!("moved\t{source}\t{destination}");
        }
        Command::Rm { remote_file } => {
            handle.delete_file(remote_file)?;
            println!("deleted\t{remote_file}");
        }
        Command::Rmdir { remote_dir } => {
            handle.delete_dir(remote_dir)?;
            println!("removed\t{remote_dir}");
        }
        Command::Upload {
            local_file,
            remote_file,
        } => {
            let data = fs::read(local_file)?;
            upload_bytes(&handle, remote_file, &data)?;
            verify_remote(&handle, remote_file, &data, VerifyMode::Size)?;
        }
        Command::Download {
            remote_file,
            local_file,
        } => {
            let data = download_bytes(&handle, remote_file)?;
            if let Some(parent) = local_file.parent() {
                fs::create_dir_all(parent)?;
            }
            fs::write(local_file, &data)?;
            println!("downloaded\t{remote_file}\t{}", local_file.display());
        }
        Command::Deploy(args) => deploy(&handle, args)?,
    }
    Ok(())
}

fn main() {
    if let Err(error) = run() {
        eprintln!("error: {error}");
        std::process::exit(1);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn splits_root_and_nested_remote_paths() {
        assert_eq!(
            remote_parts("/phy-nspire/phy-nspire.tns").unwrap(),
            ("/phy-nspire".to_string(), "phy-nspire.tns".to_string())
        );
        assert_eq!(
            remote_parts("/app.tns").unwrap(),
            ("/".to_string(), "app.tns".to_string())
        );
    }

    #[test]
    fn rejects_directory_as_remote_file() {
        assert!(remote_parts("/").is_err());
        assert!(remote_parts("/phy-nspire/").is_err());
        assert!(remote_parts("phy-nspire.tns").is_err());
    }

    #[test]
    fn creates_temporary_and_backup_siblings() {
        assert_eq!(
            sibling_path("/phy-nspire/phy-nspire.tns", ".upload").unwrap(),
            "/phy-nspire/phy-nspire.upload.tns"
        );
        assert_eq!(sibling_path("/app", ".previous").unwrap(), "/app.previous");
    }

    #[test]
    fn validates_transport_bounds() {
        assert!(TransportConfig {
            packet_size: 512,
            read_packet_size: 1440,
            ack_timeout_ms: 1500,
            handshake_timeout_ms: 5000,
            ack_retries: 4,
        }
        .validate()
        .is_ok());
        assert!(TransportConfig {
            packet_size: 63,
            read_packet_size: 1440,
            ack_timeout_ms: 1500,
            handshake_timeout_ms: 5000,
            ack_retries: 4,
        }
        .validate()
        .is_err());
        assert!(TransportConfig {
            packet_size: 512,
            read_packet_size: 1441,
            ack_timeout_ms: 1500,
            handshake_timeout_ms: 5000,
            ack_retries: 4,
        }
        .validate()
        .is_err());
        assert!(TransportConfig {
            packet_size: 512,
            read_packet_size: 1440,
            ack_timeout_ms: 49,
            handshake_timeout_ms: 5000,
            ack_retries: 4,
        }
        .validate()
        .is_err());
        assert!(TransportConfig {
            packet_size: 512,
            read_packet_size: 1440,
            ack_timeout_ms: 1500,
            handshake_timeout_ms: 49,
            ack_retries: 4,
        }
        .validate()
        .is_err());
        assert!(TransportConfig {
            packet_size: 512,
            read_packet_size: 1440,
            ack_timeout_ms: 1500,
            handshake_timeout_ms: 5000,
            ack_retries: 21,
        }
        .validate()
        .is_err());
    }

    #[test]
    fn sha256_matches_known_vector() {
        assert_eq!(
            sha256(b"abc"),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
        );
    }
}
