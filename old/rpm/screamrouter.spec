Name:           screamrouter
Version:        1.0.0
Release:        1%{?dist}
Summary:        Audio routing and management system for network audio streaming

License:        NONE
URL:            https://github.com/netham45/screamrouter
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  python3-devel
BuildRequires:  python3-setuptools
BuildRequires:  python3-pip
BuildRequires:  python3-pyyaml
BuildRequires:  git
BuildRequires:  nodejs
BuildRequires:  npm
BuildRequires:  gcc
BuildRequires:  gcc-c++
BuildRequires:  make
BuildRequires:  lame-devel
BuildRequires:  openssl
BuildRequires:  systemd-rpm-macros
BuildRequires:  cmake
BuildRequires:  autoconf
BuildRequires:  automake
BuildRequires:  libtool
BuildRequires:  glibc-devel
BuildRequires:  glibc-headers
BuildRequires:  pkgconfig
%define debug_package %{nil}

# Custom __os_install_post to prevent stripping of Windows DLLs/LIBs
# Moves the r8brain-free-src/DLL directory temporarily, runs standard brp scripts, then moves it back.
%global __os_install_post \
  mkdir -p %{buildroot}/.tmp_nostrip_files/DLL_parent && \
  (mv %{buildroot}%{_datadir}/%{name}/src/audio_engine/r8brain-free-src/DLL %{buildroot}/.tmp_nostrip_files/DLL_parent/DLL 2>/dev/null || echo "Info: r8brain DLLs not found at %{buildroot}%{_datadir}/%{name}/src/audio_engine/r8brain-free-src/DLL, or already moved. Skipping pre-strip move.") && \
  \
  %{_rpmconfigdir}/brp-compress && \
  %{_rpmconfigdir}/brp-strip %{__strip} && \
  %{_rpmconfigdir}/brp-strip-comment-note %{__strip} %{__strip_ld} && \
  %{_rpmconfigdir}/brp-strip-static-archive %{__strip} && \
  \
  (mkdir -p %{buildroot}%{_datadir}/%{name}/src/audio_engine/r8brain-free-src && \
  mv %{buildroot}/.tmp_nostrip_files/DLL_parent/DLL %{buildroot}%{_datadir}/%{name}/src/audio_engine/r8brain-free-src/DLL 2>/dev/null || echo "Info: Failed to move r8brain DLLs back from temp location, or target already exists.") && \
  rm -rf %{buildroot}/.tmp_nostrip_files && \
  \
  %{_rpmconfigdir}/brp-python-bytecompile "" 1 && \
  %{_rpmconfigdir}/brp-python-hardlink

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
%autosetup -n %{name}-%{version} -S git

%build
# Initialize submodules (for LAME, etc., used by setup.py)
git submodule update --init --recursive src/audio_engine/

# Build React site
pushd screamrouter-react
npm install
# Assuming copy-webpack-plugin is in devDependencies of package.json
npm run build 
popd
# React build output is in ./site/

# Build Python C++ extension in place
pip install "pybind11>=2.6"
python3 setup.py build_ext --inplace
# The .so file is now inside the src/ directory structure

%install
rm -rf $RPM_BUILD_ROOT

# Create directories
mkdir -p %{buildroot}%{_datadir}/%{name}/venv
mkdir -p %{buildroot}%{_datadir}/%{name}/site
mkdir -p %{buildroot}%{_datadir}/%{name}/logs
mkdir -p %{buildroot}%{_sysconfdir}/%{name}/cert
mkdir -p %{buildroot}%{_localstatedir}/log/%{name}

# Copy application files
# Copy main script, requirements, config
cp screamrouter.py %{buildroot}%{_datadir}/%{name}/
cp requirements.txt %{buildroot}%{_datadir}/%{name}/
cp uvicorn_log_config.yaml %{buildroot}%{_datadir}/%{name}/
cp setup.py pyproject.toml README.md %{buildroot}%{_datadir}/%{name}/

# Copy build_system directory (needed by setup.py)
cp -r build_system %{buildroot}%{_datadir}/%{name}/

# Copy src directory (which now includes the compiled .so extension)
# Exclude deps directory to avoid packaging build artifacts with problematic filenames
cp -r src %{buildroot}%{_datadir}/%{name}/
rm -rf %{buildroot}%{_datadir}/%{name}/src/audio_engine/deps

# Copy the built React site
cp -r site %{buildroot}%{_datadir}/%{name}/

# Copy images
cp -r images %{buildroot}%{_datadir}/%{name}/

# Install systemd service
install -D -m 644 rpm/screamrouter.service %{buildroot}%{_unitdir}/screamrouter.service

%post
# Create virtual environment
python3 -m venv %{_datadir}/%{name}/venv

# Activate virtual environment and install Python requirements and the project itself
pushd %{_datadir}/%{name}
./venv/bin/pip install --upgrade pip
./venv/bin/pip install -r requirements.txt
# Install the project (screamrouter_audio_engine) into the venv
# This will use the already built .so file from the src directory
# because setup.py will find it due to 'build_ext --inplace'
./venv/bin/pip install .
popd

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
%{_datadir}/%{name}/
%{_unitdir}/screamrouter.service
%dir %{_sysconfdir}/%{name}
%dir %{_sysconfdir}/%{name}/cert
%dir %{_localstatedir}/log/%{name}

%changelog
* Sat Mar 22 2025 ScreamRouter Maintainers <maintainers@example.com> - 1.0.0-1
- Initial RPM release
