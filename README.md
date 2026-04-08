# Paprika Tool

A small, portable C port of the Poco media downloader. Wraps `yt-dlp` and
`ffmpeg` to download YouTube and TikTok videos and to strip silence from
audio files.

Builds on **Windows** (MSVC) and **macOS** (clang, universal binary). The
GitHub Actions workflow produces ready-to-run artifacts for both.

## Build locally

```sh
cmake -B build
cmake --build build --config Release
```

The resulting binary lives at `build/paprika` (macOS) or
`build/Release/paprika.exe` (Windows / MSVC).

## First run

Paprika ships as a single binary. On first launch use the menu to install
`yt-dlp` and `ffmpeg` next to the executable — they are downloaded from the
official upstream releases via `curl`, which ships with both Windows 10+ and
macOS.

## Usage

Run with no arguments for the interactive menu:

```
paprika
```

Or pass a URL directly for scripting:

```
paprika https://www.youtube.com/watch?v=...        # default quality
paprika --1080 https://www.youtube.com/watch?v=...
paprika --audio https://www.youtube.com/watch?v=...
paprika --tiktok https://www.tiktok.com/@user/video/...
paprika --out ~/Downloads https://...
paprika --install-ytdlp
paprika --install-ffmpeg
```
