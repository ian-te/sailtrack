Name:       harbour-sailtrack
Version:    0.1.0
Release:    1
Summary:    Report the phone's location to Dawarich
License:    MIT
URL:        https://github.com/ian-te/sailtrack
Source0:    %{name}-%{version}.tar.bz2
Requires:   sailfishsilica-qt5 >= 0.10.9
Requires:   libkeepalive
Requires:   qt5-qtpositioning
Requires:   qt5-plugin-position-geoclue
BuildRequires: pkgconfig(sailfishapp) >= 1.0.2
BuildRequires: pkgconfig(Qt5Core)
BuildRequires: pkgconfig(Qt5Qml)
BuildRequires: pkgconfig(Qt5Quick)
BuildRequires: pkgconfig(Qt5DBus)
BuildRequires: pkgconfig(Qt5Network)
BuildRequires: pkgconfig(Qt5Positioning)
BuildRequires: pkgconfig(keepalive)
BuildRequires: desktop-file-utils

%description
A background daemon that wakes every few minutes, takes one position fix and
uploads it to a self-hosted Dawarich through its OwnTracks endpoint, queueing
points while offline, and a Silica app to configure and watch it.

%prep
%autosetup

%build
%qmake5 VERSION=%{version}
%make_build

%install
%qmake5_install
desktop-file-install --delete-original --dir %{buildroot}%{_datadir}/applications %{buildroot}%{_datadir}/applications/*.desktop

%files
%defattr(-,root,root,-)
%{_bindir}/%{name}
%{_bindir}/%{name}-daemon
%{_userunitdir}/%{name}.service
%{_datadir}/%{name}
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/*/apps/%{name}.png
