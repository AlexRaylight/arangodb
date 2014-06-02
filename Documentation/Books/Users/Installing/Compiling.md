<a name="compiling_arangodb_from_scratch"></a>
# Compiling ArangoDB from scratch

The following sections describe how to compile and build the ArangoDB from
scratch. The ArangoDB will compile on most Linux and Mac OS X systems. It
assumes that you use the GNU C/C++ compiler or clang/clang++ to compile the 
source. ArangoDB has been tested with the GNU C/C++ compiler and clang/clang++, 
but should be able to compile with any Posix-compliant compiler. 
Please let us know whether you successfully compiled it with another C/C++ 
compiler.

There are the following possibilities:

- all-in-one: this version contains the source code of the ArangoDB, all 
 generated files from the autotools, FLEX, and BISON as well 
 as a version of V8, libev, and ICU.

- devel: this version contains the development version of the ArangoDB.
  Use this branch, if you want to make changes to ArangoDB 
  source.

The devel version requires a complete development environment, while the
all-in-one version allows you to compile the ArangoDB without installing all the
prerequisites. The disadvantage is that it takes longer to compile and you
cannot make changes to the flex or bison files.

<a name="amazon_micro_instance"></a>
### Amazon Micro Instance

@sohgoh has reported that it is very easy to install ArangoDB on an Amazon
Micro Instance:

    amazon> sudo yum install readline-devel
    amazon> ./configure
    amazon> make
    amazon> make install

For detailed instructions the following section.

<a name="all-in-one_version"></a>
## All-In-One Version

Note: there are separate instructions for the **devel** version in the next section.

<a name="basic_system_requirements"></a>
### Basic System Requirements

Verify that your system contains:

- the GNU C/C++ compilers "gcc" and "g++" and the standard C/C++ libraries. You will 
  compiler and library support for C++11. To be on the safe side with gcc/g++, you will
  need version number 4.8 or higher. For "clang" and "clang++", you will need at least
  version 3.4.
- the GNU make

In addition you will need the following library:

- the GNU readline library
- the OpenSSL library
- Go 1.2 (or higher)

Under Mac OS X you also need to install:

- Xcode
- scons

<a name="download_the_source"></a>
### Download the Source

Download the latest source using GIT:
    
    git clone git://github.com/triAGENS/ArangoDB.git

Note: if you only plan to compile ArangoDB locally and do not want to modify or push
any changes, you can speed up cloning substantially by using the `--single-branch` and 
`--depth` parameters for the clone command as follws:
    
    git clone --single-branch --depth 1 git://github.com/triAGENS/ArangoDB.git

<a name="configure"></a>
### Configure

Switch into the ArangoDB directory

    cd ArangoDB

In order to configure the build environment execute

    ./configure --enable-all-in-one-v8 --enable-all-in-one-libev --enable-all-in-one-icu

to setup the makefiles. This will check the various system characteristics and
installed libraries.

<a name="compile"></a>
### Compile

Compile the program by executing

    make

This will compile the ArangoDB and create a binary of the server in

    ./bin/arangod

<a name="test"></a>
### Test

Create an empty directory

    unix> mkdir /tmp/database-dir

Check the binary by starting it using the command line.

    unix> ./bin/arangod -c etc/relative/arangod.conf --server.endpoint tcp://127.0.0.1:12345 --server.disable-authentication true /tmp/database-dir

This will start up the ArangoDB and listen for HTTP requests on port 12345 bound
to IP address 127.0.0.1. You should see the startup messages similar to the following:

2013-10-14T12:47:29Z [29266] INFO ArangoDB xxx ... </br>
2013-10-14T12:47:29Z [29266] INFO using endpoint 'tcp://127.0.0.1.12345' for non-encrypted requests </br>
2013-10-14T12:47:30Z [29266] INFO Authentication is turned off </br>
2013-10-14T12:47:30Z [29266] INFO ArangoDB (version xxx) is ready for business. Have fun! </br>

If it fails with a message about the database directory, please make sure the
database directory you specified exists and can be written into.

Use your favorite browser to access the URL

    http://127.0.0.1:12345/_api/version

This should produce a JSON object like

    {"server" : "arango", "version" : "..."}

as result.

<a name="install"></a>
### Install

Install everything by executing

    make install

You must be root to do this or at least have write permission to the
corresponding directories.

The server will by default be installed in

    /usr/local/sbin/arangod

The configuration file will be installed in

    /usr/local/etc/arangodb/arangod.conf

The database will be installed in

    /usr/local/var/lib/arangodb

The ArangoShell will be installed in

    /usr/local/bin/arangosh

When upgrading from a previous version of ArangoDB, please make sure you inspect ArangoDB's
log file after an upgrade. It may also be necessary to start ArangoDB with the `--upgrade`
parameter once to perform required upgrade or initialisation tasks.

<a name="devel_version"></a>
## Devel Version

<a name="basic_system_requirements"></a>
### Basic System Requirements

Verify that your system contains

- the GNU C/C++ compilers "gcc" and "g++" and the standard C/C++ libraries. You will 
  compiler and library support for C++11. To be on the safe side with gcc/g++, you will
  need version number 4.8 or higher. For "clang" and "clang++", you will need at least
  version 3.4.
- the GNU autotools (autoconf, automake)
- the GNU make
- the GNU scanner generator FLEX, at least version 2.3.35
- the GNU parser generator BISON, at least version 2.4
- Python, version 2 or 3
- Go, version 1.2 or higher

In addition you will need the following libraries

- libev in version 3 or 4 (only when configured with `--disable-all-in-one-libev`)
- Google's V8 engine (only when configured with `--disable-all-in-one-v8`)
- the ICU library (only when not configured with `--enable-all-in-one-icu`)
- the GNU readline library
- the OpenSSL library
- the Boost test framework library (boost_unit_test_framework)

To compile Google V8 yourself, you will also need Python 2 and SCons.

Some distributions, for example Centos 5, provide only very out-dated versions
of compilers, FLEX, BISON, and the V8 engine. In that case you need to compile newer
versions of the programs and/or libraries.

If necessary, install or download the prerequisites:

- GNU C/C++ 4.8 or higher (see http://gcc.gnu.org) 
- Google's V8 engine (see http://code.google.com/p/v8)
- SCons for compiling V8 (see http://www.scons.org)
- libev (see http://software.schmorp.de/pkg/libev.html) 
- OpenSSL (http://openssl.org/)
- Go (http://golang.org/)

Most linux systems already supply RPM or DEP for these
packages. Please note that you have to install the development packages.

<a name="download_the_source"></a>
### Download the Source

Download the latest source using GIT:

    git clone git://github.com/triAGENS/ArangoDB.git

<a name="setup"></a>
### Setup

Switch into the ArangoDB directory

    cd ArangoDB

The source tarball contains a pre-generated "configure" script. You can
regenerate this script by using the GNU auto tools. In order to do so, execute

    make setup

This will call aclocal, autoheader, automake, and autoconf in the correct order.

<a name="configure"></a>
### Configure

In order to configure the build environment please execute

    unix> ./configure --enable-all-in-one-v8 --enable-all-in-one-libev --enable-all-in-one-icu 

to setup the makefiles. This will check for the various system characteristics
and installed libraries.

Please note that it may be required to set the `--host` and `--target` variables when 
running the configure command. For example, if you compile on MacOS, you should add the 
following options to the configure command:

    --host=x86_64-apple-darwin --target=x86_64-apple-darwin

The host and target values for other architectures vary. 

Now continue with @ref CompilingAIOCompile.

If you also plan to make changes to the source code of ArangoDB, add the following
option to the `configure` command: `--enable-maintainer-mode`. Using this option,
you can make changes to the lexer and parser files and some other source files that
will generate other files. Enabling this option will add extra dependencies to
BISON, FLEX, and PYTHON. These external tools then need to be available in the 
correct versions on your system.

The following configuration options exist:

`--enable-relative` will make relative paths be used in the compiled binaries and
scripts. It allows to run ArangoDB from the compile directory directly, without the
need for a `make install` command and specifying much configuration parameters. 
When used, you can start ArangoDB using this command:

    bin/arangod /tmp/database-dir

ArangoDB will then automatically use the configuration from file `etc/relative/arangod.conf`.

`--enable-all-in-one-libev` tells the build system to use the bundled version
of libev instead of using the system version.

`--disable-all-in-one-libev` tells the build system to use the installed
system version of libev instead of compiling the supplied version from the
3rdParty directory in the make run.

`--enable-all-in-one-v8` tells the build system to use the bundled version of
V8 instead of using the system version.

`--disable-all-in-one-v8` tells the build system to use the installed system
version of V8 instead of compiling the supplied version from the 3rdParty
directory in the make run.

`--enable-all-in-one-icu` tells the build system to use the bundled version of
ICU instead of using the system version.

`--disable-all-in-one-icu` tells the build system to use the installed system
version of ICU instead of compiling the supplied version from the 3rdParty
directory in the make run.

`--enable-all-in-one-boost` tells the build system to use the bundled version of
Boost header files. This is the default and recommended.

`--enable-all-in-one-etcd` tells the build system to use the bundled version 
of ETCD. This is the default and recommended.

`--enable-internal-go` tells the build system to use Go binaries located in the
3rdParty directory. Note that ArangoDB does not ship with Go binaries, and that
the Go binaries must be copied into this directory manually.

`--enable-maintainer-mode` tells the build system to use BISON and FLEX to
regenerate the parser and scanner files. If disabled, the supplied files will be
used so you cannot make changes to the parser and scanner files.  You need at
least BISON 2.4.1 and FLEX 2.5.35.  This option also allows you to make changes
to the error messages file, which is converted to js and C header files using
Python. You will need Python 2 or 3 for this.  Furthermore, this option enables
additional test cases to be executed in a `make unittests` run. You also need to
install the Boost test framework for this.

<a name="compiling_go"></a>
### Compiling Go

Users F21 and duralog told us that some systems don't provide an 
update-to-date version of go. This seems to be the case for at least Ubuntu 
12 and 13. To install go on these system, you may follow the instructions 
provided [here](http://blog.labix.org/2013/06/15/in-flight-deb-packages-of-go).
For other systems, you may follow the instructions 
[here](http://golang.org/doc/install).

To make ArangoDB use a specific version of go, you may copy the go binaries
into the 3rdParty/go-32 or 3rdParty/go-64 directories of ArangoDB (depending
on your architecture), and then tell ArangoDB to use this specific go version
by using the `--enable-internal-go` configure option.

User duralog provided some the following script to pull the latest release 
version of go into the ArangoDB source directory and build it:

    cd ArangoDB
    hg clone -u release https://code.google.com/p/go 3rdParty/go-64 && \
      cd 3rdParty/go-64/src && \
      ./all.bash

    # now that go is installed, run your configure with --enable-internal-go
    ./configure\
      --enable-all-in-one-v8 \
      --enable-all-in-one-libev \
      --enable-internal-go

