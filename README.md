# Biopass - An alternative to Howdy

<p align="center">
    <img src="https://public-r2.ticklab.site/media/tc1oN21KXhMM1B2jOecRhk=" alt="Biopass Logo" width="120" />
</p>

<p align="center">
    <a href="https://github.com/phucvinh57/biopass/releases/latest">
        <img src="https://img.shields.io/github/v/release/phucvinh57/biopass?label=Last%20Release&style=flat-square" alt="Latest release" />
    </a>
    <a href="https://github.com/phucvinh57/biopass/stargazers">
        <img src="https://img.shields.io/github/stars/phucvinh57/biopass?style=flat-square" alt="GitHub stars" />
    </a>
    <a href="https://github.com/phucvinh57/biopass/issues">
        <img src="https://img.shields.io/github/issues/phucvinh57/biopass?style=flat-square" alt="Open Issues" />
    </a>
</p>

<p align="center"><b>Modern multi-modal biometric login for Linux — face and fingerprint, with GUI management.</b></p>

---

## Why Biopass?

While Windows Hello provides a seamless multi-modal biometric experience (Face, Fingerprint, PIN) on Windows 11, Linux has historically lacked a modern, unified equivalent. Biopass was developed by [@phucvinh57](https://github.com/phucvinh57) and [@thaitran24](https://github.com/thaitran24) to fill that gap — see the comparison with [Howdy](https://github.com/boltgolt/howdy) below.

## Comparison

| Feature | [**Biopass**](https://github.com/phucvinh57/biopass) | [**Howdy**](https://github.com/boltgolt/howdy) |
| :--- | :--- | :--- |
| **Authentication methods** | Face + Fingerprint | Face only |
| **User Interface** | Modern GUI for management | Command-line interface only |
| **Configuration** | GUI | Manual |
| **Face Anti-spoofing** | IR camera + Embedded AI model | IR camera only |

## Installation

- Debian and RPM package: https://github.com/phucvinh57/biopass/releases
- AUR package: https://aur.archlinux.org/packages/biopass-bin
- System sign-in setup uses distro-managed PAM configuration when available (for example `pam-auth-update` on Debian/Ubuntu): [docs/PAM.md](docs/PAM.md)
- Interactive `polkit` authentication setup: [docs/Polkit.md](docs/Polkit.md)
- [IR camera setup guide](docs/IR%20camera.md)

## Features

- [x] Authentication: User can register multiple biometrics for authentication. Authentication methods can be executed in parallel or sequentially.
    - [x] Face:
      - [x] Recognition
      - [x] Anti-spoofing
        - [x] With AI model
        - [x] With IR camera
    - [x] Fingerprint
- [ ] Local AI model management: User can download, update, and delete AI models for supported authentication methods.

Feel free to request new features or report bugs by opening an issue. For contributing, please read [CONTRIBUTING.md](CONTRIBUTING.md).

## References
Models used in this project:
- Face Recognition: **[EdgeFace](https://github.com/otroshi/edgeface)**
- Face Detection: **[YOLO-Face](https://github.com/akanametov/yolo-face)**

## Star History

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="assets/star-history-dark.svg" />
  <source media="(prefers-color-scheme: light)" srcset="assets/star-history-light.svg" />
  <img alt="Star History Chart" src="assets/star-history-light.svg" />
</picture>
