Vendor:       Edward Groenendaal
Name:         swdiag
Release:      1
License:      MIT
Group:        unsorted
Provides:     swdiag
Packager:     eddyg@myreflection.org
Version:      0.0.1
Summary:      Monitoring and Diagnostics Facility
Source:       swdiag-0.0.1.tar.gz 
BuildRoot:    /var/tmp/%{name}-buildroot
%description
Diagnostic and Monitoring for Linux
%prep
%setup
%build
./configure --prefix=/usr --prefix=/usr --mandir=/usr/share/man --infodir=/usr/share/info --sysconfdir=/etc/swdiag
make
%install
rm -rf $RPM_BUILD_ROOT
make DESTDIR=$RPM_BUILD_ROOT install
%files
%defattr(-,root,root)
%config /etc/swdiag/swdiag.cfg
%doc ChangeLog
%doc README
%doc TODO
%doc LICENSE
%doc AUTHORS
/usr/bin/swdiag
/usr/share/swdiag/server/modules
/usr/share/swdiag/server/http
/usr/include/swdiag/project.h
/usr/include/swdiag/swdiag_client.h
/usr/lib/libswdiag.a
/usr/lib/libswdiag.la
/usr/lib/libswdiag.so
/usr/lib/libswdiag.so.1
/usr/lib/libswdiag.so.1.0.0

