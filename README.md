# mod_vnc

FreeSWITCH module to interace with VNC.

This mod was written many years ago when I learning to write FreeSWITCH modules. It probably doesn't build on the current version of FreeSWITCH.

The code is simple and hope still useful.

Pull Request is welcome.

## ToDo:

* [ ] Fix build on latest FreeSWITCH master
* [ ] Add cmake or autotools build scripts
* [ ] Add some test cases

## build

Put this mod in the freeswitch source dir, your freeswitch should already been built and installed.

```
cd freeswitch/src/mod/
mkdir rts
cd rts
git clone https://github.com/rts-cn/mod_vnc
cd mod_vnc
make
make install
```

## load

```
load mod_vnc
```

## FAQ

Q: What License?

A: Same as FreeSWITCH.

Q: Do you accept Pull Request?

A: Sure. Thanks.
