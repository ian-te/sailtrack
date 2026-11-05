Name:       harbour-sailtrack
# Builds take their version from the git tag vX.Y.Z: CI and OBS's tar_git rewrite this line,
# and sfdk uses the newest tag. Set it to the release in the commit you tag (see the README).
Version:    0.2.0
Release:    1
Summary:    Report the phone's location to Dawarich
License:    MIT
URL:        https://github.com/ian-te/sailtrack
Source0:    %{name}-%{version}.tar.bz2
Requires:   sailfishsilica-qt5 >= 0.10.9
Requires:   libkeepalive
Requires:   qt5-qtpositioning
Requires:   qt5-plugin-position-geoclue
Requires:   qt5-qtsensors
Requires(post): systemd
Requires(postun): systemd
BuildRequires: pkgconfig(sailfishapp) >= 1.0.2
BuildRequires: pkgconfig(Qt5Core)
BuildRequires: pkgconfig(Qt5Qml)
BuildRequires: pkgconfig(Qt5Quick)
BuildRequires: pkgconfig(Qt5DBus)
BuildRequires: pkgconfig(Qt5Network)
BuildRequires: pkgconfig(Qt5Positioning)
BuildRequires: pkgconfig(Qt5Sensors)
BuildRequires: pkgconfig(keepalive)
BuildRequires: desktop-file-utils

%description
A background daemon that wakes every few minutes, takes one position fix and
uploads it to a self-hosted Dawarich through its OwnTracks endpoint, queueing
points while offline, and a Silica app to configure and watch it.

# Metadata for SailfishOS:Chum, see
# https://github.com/sailfishos-chum/main/blob/main/Metadata.md
%if 0%{?_chum}
Title: Sailtrack
Type: desktop-application
DeveloperName: Ian Te
Categories:
 - Utility
 - Maps
Custom:
  Repo: https://github.com/ian-te/sailtrack
PackageIcon: https://github.com/ian-te/sailtrack/raw/main/app/icons/harbour-sailtrack.svg
AIRating: V
AINote: Written almost entirely by Claude (Anthropic), tested by the developer on a Jolla Phone
Links:
  Homepage: https://github.com/ian-te/sailtrack
  Bugtracker: https://github.com/ian-te/sailtrack/issues
%endif

%prep
%autosetup

%build
%qmake5 VERSION=%{version}
%make_build

%install
%qmake5_install
desktop-file-install --delete-original --dir %{buildroot}%{_datadir}/applications %{buildroot}%{_datadir}/applications/*.desktop

# systemctl-user runs systemctl in the active user session, as Jolla's own packages do: a new or
# changed unit is picked up and an upgraded daemon restarts, with no step left for the user.
%post
systemctl-user daemon-reload || :
if [ "$1" -ge 2 ]; then
    systemctl-user try-restart %{name}.service || :
fi

%preun
if [ "$1" -eq 0 ]; then
    systemctl-user stop %{name}.service || :
fi

%postun
systemctl-user daemon-reload || :

%files
%defattr(-,root,root,-)
%{_bindir}/%{name}
%{_bindir}/%{name}-daemon
%{_prefix}/lib/systemd/user/%{name}.service
%{_datadir}/%{name}
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/*/apps/%{name}.png
