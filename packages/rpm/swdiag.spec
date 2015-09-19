Vendor:       Edward Groenendaal
Name:         myreflection
Release:      1
License:      MIT
Group:        unsorted
Provides:     myreflection
Packager:     eddyg@myreflection.org
Version:      0.0.1
Summary:      Monitoring and Diagnostics Facility
Source:       myreflection-0.0.1.tar.gz 
BuildRoot:    /var/tmp/%{name}-buildroot
%description
Diagnostic and Monitoring for Linux
%prep
%setup
%build
./configure --prefix=/usr --prefix=/usr --mandir=/usr/share/man --infodir=/usr/share/info --sysconfdir=/etc/myreflection
make
%install
rm -rf $RPM_BUILD_ROOT
make DESTDIR=$RPM_BUILD_ROOT install
%files
%defattr(-,root,root)
%config /etc/myreflection/myreflection.cfg
%doc ChangeLog
%doc README
%doc TODO
%doc LICENSE
%doc AUTHORS
/usr/bin/myreflection
/usr/share/myreflection/server/modules
/usr/share/myreflection/server/http
/usr/include/myreflection/project.h
/usr/include/myreflection/swdiag_client.h
/usr/lib/libmyreflection.a
/usr/lib/libmyreflection.la
/usr/lib/libmyreflection.so
/usr/lib/libmyreflection.so.1
/usr/lib/libmyreflection.so.1.0.0

