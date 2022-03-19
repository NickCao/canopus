project('canopus', 'cpp')
nixexpr = dependency('nix-expr')
seccomp = dependency('libseccomp')
executable('canopus', ['canopus.cc'], install : true, dependencies : [nixexpr,seccomp])
