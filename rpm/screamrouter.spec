Name:           screamrouter
Version:        1.0.0
Release:        1%{?dist}
Summary:        Audio routing and management system for network audio streaming

License:        GPLv3+
URL:            https://github.com/netham45/screamrouter
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  python3-devel
BuildRequires:  python3-setuptools
BuildRequires:  python3-pip
BuildRequires:  nodejs
BuildRequires:  npm
BuildRequires:  gcc
BuildRequires:  gcc-c++
BuildRequires:  make
BuildRequires:  lame-devel
BuildRequires:  openssl
BuildRequires:  systemd-rpm-macros

Requires:       python3
Requires:       python3-pip
Requires:       python3-virtualenv
Requires:       lame
Requires:       lame-libs
Requires:       openssl

%description
ScreamRouter is a versatile audio routing and management system with a 
Python frontend/configuration layer and C++ backend, designed for
network audio streaming. It supports Scream and RTP audio sources, 
along with Scream receivers and web-based MP3 streamers.

Features include:
* Audio Routing and Configuration
* Audio Processing and Playback
* Integration and Compatibility with Home Assistant
* System Management

%prep
%autosetup -n %{name}-%{version}

%build
# Build C utilities
cd c_utils
./build.sh
cd ..

# Build React site
cd screamrouter-react
npm install
npm install --save-dev copy-webpack-plugin
npm run build
cd ..

%install
rm -rf $RPM_BUILD_ROOT

# Create directories
mkdir -p %{buildroot}%{_datadir}/%{name}
mkdir -p %{buildroot}%{_datadir}/%{name}/venv
mkdir -p %{buildroot}%{_datadir}/%{name}/c_utils/bin
mkdir -p %{buildroot}%{_datadir}/%{name}/site
mkdir -p %{buildroot}%{_datadir}/%{name}/logs
mkdir -p %{buildroot}%{_sysconfdir}/%{name}
mkdir -p %{buildroot}%{_sysconfdir}/%{name}/cert
mkdir -p %{buildroot}%{_localstatedir}/log/%{name}

# Copy files
cp -r screamrouter.py %{buildroot}%{_datadir}/%{name}/
cp -r requirements.txt %{buildroot}%{_datadir}/%{name}/
cp -r uvicorn_log_config.yaml %{buildroot}%{_datadir}/%{name}/
cp -r site/ %{buildroot}%{_datadir}/%{name}/
cp -r src/ %{buildroot}%{_datadir}/%{name}/
cp -r c_utils/bin/* %{buildroot}%{_datadir}/%{name}/c_utils/bin/
cp -r images/ %{buildroot}%{_datadir}/%{name}/
cp -r screamrouter-react/build/* %{buildroot}%{_datadir}/%{name}/site/

# Install systemd service
install -D -m 644 rpm/screamrouter.service %{buildroot}%{_unitdir}/screamrouter.service

%post
# Create virtual environment
python3 -m venv %{_datadir}/%{name}/venv

# Activate virtual environment and install Python requirements
%{_datadir}/%{name}/venv/bin/pip install --upgrade pip
%{_datadir}/%{name}/venv/bin/pip install -r %{_datadir}/%{name}/requirements.txt

# Generate SSL certificates if they don't exist
if [ ! -f %{_sysconfdir}/%{name}/cert/cert.pem ] || [ ! -f %{_sysconfdir}/%{name}/cert/privkey.pem ]; then
    echo "Generating self-signed SSL certificate..."
    openssl req -new -newkey rsa:4096 -days 365 -nodes -x509 \
      -subj "/C=US/ST=State/L=City/O=Organization/CN=screamrouter" \
      -keyout %{_sysconfdir}/%{name}/cert/privkey.pem -out %{_sysconfdir}/%{name}/cert/cert.pem
fi

# Create symlink to certificate directory
ln -sf %{_sysconfdir}/%{name}/cert %{_datadir}/%{name}/cert

%systemd_post screamrouter.service

%preun
%systemd_preun screamrouter.service

%postun
%systemd_postun_with_restart screamrouter.service

%files
%license COPYING
%{_datadir}/%{name}/
%{_unitdir}/screamrouter.service
%dir %{_sysconfdir}/%{name}
%dir %{_sysconfdir}/%{name}/cert
%dir %{_localstatedir}/log/%{name}

%changelog
* Sat Mar 22 2025 ScreamRouter Maintainers <maintainers@example.com> - 1.0.0-1
- Initial RPM release