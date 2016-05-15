PROJ=	tlsf

all: rpm

rpm:
	mkdir -p SOURCES && tar czpvf SOURCES/lib$(PROJ).tar.gz ../src
	rpmbuild -ba -v --define "_topdir ${PWD}" SPECS/lib$(PROJ).spec

clean:
	rm -rf BUILD BUILDROOT RPMS SOURCES SRPMS

.PHONY: all rpm clean
