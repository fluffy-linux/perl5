#!./perl -IFoo::Bar -IBla

BEGIN {
    chdir 't' if -d 't';
    unshift @INC, '../lib';     # Do NOT make this @INC = '../lib';
    require './test.pl';	# for which_perl() etc
    plan(9);
}

my $Is_VMS   = $^O eq 'VMS';
my $lib;

$lib = 'Bla';
ok do { grep { $_ eq $lib } @INC[0..($#INC-1)] }, 'Identified entry in @INC';
SKIP: {
  skip 'Double colons not allowed in dir spec', 1 if $Is_VMS;
  $lib = 'Foo::Bar';
  ok do { grep { $_ eq $lib } @INC[0..($#INC-1)] },
    'Identified entry in @INC with double colons';
}

$lib = 'Bla2';
fresh_perl_is("print grep { \$_ eq '$lib' } \@INC[0..(\$#INC-1)]", $lib,
	      { switches => ['-IBla2'], nolib => 1 }, '-I');
SKIP: {
  skip 'Double colons not allowed in dir spec', 1 if $Is_VMS;
  $lib = 'Foo::Bar2';
  fresh_perl_is("print grep { \$_ eq '$lib' } \@INC", $lib,
	        { switches => ['-IFoo::Bar2'], nolib => 1 }, '-I with colons');
}

SKIP: {
  skip 'POSIX wildcard @INC test', 5 if $^O eq 'MSWin32' || $Is_VMS;
  require File::Temp;
  my $td = File::Temp::tempdir(CLEANUP => 1);
  mkdir "$td/pkgA" or die $!;
  mkdir "$td/pkgA/lib" or die $!;
  mkdir "$td/pkgB" or die $!;
  mkdir "$td/pkgB/lib" or die $!;
  open my $sf, '>', "$td/pkgC.squashfs" or die $!;
  close $sf;

  fresh_perl_is(
    'print join("|", grep { index($_, q(' . $td . ')) == 0 } @INC)',
    "$td/pkgA/lib|$td/pkgB/lib",
    { switches => ["-I$td/*/lib"], nolib => 1 },
    '-I expands wildcard directory entries in order'
  );

  {
    local $ENV{PERL5OPT} = "-I$td/*/lib";
    fresh_perl_is(
      'print join("|", grep { index($_, q(' . $td . ')) == 0 } @INC)',
      "$td/pkgA/lib|$td/pkgB/lib",
      { nolib => 1 },
      '-I in PERL5OPT (unshifted) expands wildcard entries in order'
    );
  }

  local $ENV{PERL5LIB} = "$td/*/lib:$td/no_such_pkg/*/lib";
  fresh_perl_is(
    'print join("|", grep { index($_, q(' . $td . ')) == 0 } @INC)',
    "$td/pkgA/lib|$td/pkgB/lib",
    { nolib => 1 },
    'PERL5LIB expands wildcard directory entries and drops unmatched wildcards'
  );

  mkdir "$td/1:dir" or die $!;
  mkdir "$td/1:dir/lib" or die $!;
  mkdir "$td/[unclosed" or die $!;
  mkdir "$td/[unclosed/lib" or die $!;
  local $ENV{PERL5LIB} = "$td/[unclosed/lib:$td/1[x:]dir/lib:$td/[[:alpha:]]*/lib";
  fresh_perl_is(
    'print join("|", grep { index($_, q(' . $td . ')) == 0 } @INC)',
    "$td/[unclosed/lib|$td/1:dir/lib|$td/pkgA/lib|$td/pkgB/lib",
    { nolib => 1 },
    'PERL5LIB splits colons with single-component bracket awareness'
  );

  local $ENV{PERL5LIB} = "$td/[!x]kgA/lib:$td/[]p]kgB/lib";
  fresh_perl_is(
    'print join("|", grep { index($_, q(' . $td . ')) == 0 } @INC)',
    "$td/pkgA/lib|$td/pkgB/lib",
    { nolib => 1 },
    'PERL5LIB splits around negated and ]-first bracket expressions'
  );
}
