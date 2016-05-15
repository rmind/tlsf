Name:		libtlsf
Version:	0.1
Release:	1%{?dist}
Summary:	TLSF: two-level segregated fit O(1) allocator
Group:		System Environment/Libraries
License:	BSD
URL:		https://github.com/rmind/tlsf
Source0:	libtlsf.tar.gz

BuildRequires:	make
BuildRequires:	libtool

%description

TLSF: two-level segregated fit allocator which guarantees O(1) time.
This implementation also provides a variation, let's call it TLSF-EXT,
supporting the externalised block header allocation.  Therefore, it can
be used to manage arbitrary resources, e.g. address or disk space.

%prep
%setup -q -n src

%build
make %{?_smp_mflags} LIBDIR=%{_libdir}

%install
make install \
    DESTDIR=%{buildroot} \
    LIBDIR=%{_libdir} \
    INCDIR=%{_includedir} \
    MANDIR=%{_mandir}

%files
%{_libdir}/*
%{_includedir}/*
#%{_mandir}/*

%changelog
