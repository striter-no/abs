#include "abs/colors.h"
#include <abs/compilation.h>
#include <abs/configuration.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

void usage(const char *prog){
	printf(
		"usage: %s [PATH] [-h/--help] [gen]"
		"\n\nPATH - path to configuration, by default 'abs.conf'\n"
		"-h/--help - show this message and exit\n"
		"-d/--docs - show more help about configuration\n"
		"gen - generate default config in current directory\n", prog);
	exit(EXIT_SUCCESS);
}

void gen(){
	const char default_cfg[] = 
"[project]\n"
"name = project\n"
"version = 0.0.1\n"
"\n"
"[files]\n"
"sources = main.c\n"
"output = main\n"
"\n"
"[compiler]\n"
"cc = gcc\n"
"\n"
"[flags]\n"
"common = -std=c11 -Wall -Wextra -Wpedantic\n"
"\n"
;
	FILE *o = fopen("./abs.conf", "w");
	if (!o) exit(-1);

	fwrite(default_cfg, strlen(default_cfg), 1, o);

	fclose(o);
}

void docs(){
	const char docs_str[] = 
"Sections:\n"
"- project:      name and version of the project\n"
"- modules:      list of submodules to build before\n"
"- compiler:     which compiler to use\n"
"- modes:        sets active build mode (debug/release)\n"
"- dependencies: set of PKG config libs and static/dynamic libs\n"
"- defines:      NAME=VALUE list for defines in program\n"
"- flags:        common and security flags for building\n"
"- files:        source files and output file\n"
"- dirs:         directories for source, output, include and lib files\n"
"- mode.debug:   flags and security options on debug mode\n"
"- mode.release: flags and security options on release mode\n"
"\n"
"Per-section documentation\n"
"PROJECT\n"
"- name: string, name of the project\n"
"- version: string, version of the project\n"
"\n"
"COMPILER\n"
"- cc:      path, program, which used for compiling sources\n"
"- built:   binary (default), static, shared - type of build\n"
"- phase:   all (default), compile, link - type of building\n"
"           (link - compile *.o files in objs dir, compile -\n"
"           generate *.o files)\n"
"- cleanup: clean objs directory or not (default: true)\n"
"\n"
"FLAGS\n"
"- common: list[str], space-splitted enumeration of flags\n"
"  which are used in all modes\n"
"- hardening: list[str], enumeration of flags, which are \n"
"  used when security is enabled\n"
"\n"
"FILES\n"
"- sources: enumeration (globs enabled) of all *.c files,\n"
"  used by compiler\n"
"- output:  output binary file from compiler\n"
"\n"
"DIRS\n"
"- output:   directory to store binary files\n"
"- src:      directory where source files stored\n"
"- includes: directory where header files stored\n"
"- libs:     directory where library files stored\n"
"- objects:  directory where *.o files are stored\n"
"\n"
"MODES\n"
"- active: active `debug` or `release`, changes mode.debug/release\n"
"  choice\n"
"\n"
"DEPENDENCIES\n"
"- pkgs_path: PKG_CONFIG_PATH variable\n"
"- pkgs:      list of pkgconfig packages to include\n"
"- libs:      list of libraries (static/dynamic) to use\n"
"\n"
"MODE.DEBUG\n"
"- flags: additional flags when building in debug mode\n"
"- security: bool, `true` or `false`, enables harderning flags\n"
"   if set to true\n"
"\n"
"MODE.RELEASE\n"
"- flags: additional flags when building in release mode\n"
"- security: bool, `true` or `false`, enables harderning flags\n"
"   if set to true\n"
"DEFINES\n"
"- list of elements like `KEY = VALUE` that are passed to program\n"
"  in -D...=... format\n"
"\n"
"MODULES\n"
"- list of elements like `MODULE_NAME = MODULE_DIR, MODULE_CONFIG`\n"
"  for example\n"
"```\n"
"[modules]\n"
"test = code/include/submodule, abs.conf\n"
"```\n"
"\n"

;
	printf("Documentation:\n%s\n", docs_str);
}

int build(const char *prog, const char *confpath){
	char *config_dir = get_dir_from_path(confpath);
	if (!config_dir) {
        fprintf(stderr, "%sfailed%s to determine config directory\n", abs_fore.red, abs_fore.normal);
        return 1;
    }

	char resolved[PATH_MAX];
	realpath(config_dir, resolved);
	free(config_dir);

	ini_config conf;
	if (0 > ini_load_file(&conf, confpath)){
		fprintf(stderr, "%sfailed%s to load configuration: %s%s%s\naborting\n", abs_fore.red, abs_fore.normal, abs_fore.gray, confpath, abs_fore.normal);
		return -1;
	}

	char *MAIN_DIR = getenv("MAIN_DIR");
	const char *prj_name = ini_get_at(&conf, "project", "name");
	const char *prj_ver = ini_get_at(&conf, "project", "version");
	if (!MAIN_DIR && prj_name){
		printf("Building project %s%s%s\n", abs_fore.yellow, prj_name, abs_fore.normal);
	}
	if (!MAIN_DIR && prj_ver){
		printf("Version %s%s%s\n", abs_fore.blue, prj_ver, abs_fore.normal);
	}

	build_modules(prog, resolved, &conf);

	// no files
	if (ini_check(&conf, "files")){
		goto _end;
	}

	compiler_conf cconf;
	memset(&cconf, 0, sizeof(cconf));
	config_ini_parse(&conf, &cconf);
	
	char cmd[15000] = {0};
    
	if (MAIN_DIR){
		snprintf(cmd, 15000, "MAIN_DIR=%s ", MAIN_DIR);
	}
	
	build_config_emit_cmd(&cconf, cmd + strlen(cmd), sizeof(cmd) - strlen(cmd));
    
    printf("%s[gen]%s command: %s%s%s\n", abs_fore.blue, abs_fore.normal, abs_fore.gray, cmd, abs_fore.normal);
	int r = system(cmd);

	if (r == 0 && !MAIN_DIR){
		printf("%s[gen]%s: %s: build %sSUCCESS%s\n", abs_fore.blue, abs_fore.normal, prj_name ? prj_name: "<program>", abs_fore.green, abs_fore.normal);
	} else if (!MAIN_DIR){
		printf("%s[gen]%s: %s: build %sFAIL%s\n", abs_fore.blue, abs_fore.normal, prj_name ? prj_name: "<program>", abs_fore.red, abs_fore.normal);
		exit(-1);
	}

	compiler_conf_free(&cconf);

_end:
	ini_clear_config(&conf);
	return 0;
}

int main(int argc, const char *argv[]){
	if (argc == 1){
		return build(argv[0], "abs.conf");
	}

	if (argc == 2) {
		if (strcmp("-h", argv[1]) == 0 || strcmp("--help", argv[1]) == 0){
			usage(argv[0]);
		}

		if (strcmp("gen", argv[1]) == 0){
			gen();
			return 0;
		}

		if (strcmp("--docs", argv[1]) == 0 || strcmp("-d", argv[1]) == 0){
			docs();
			return 0;
		}
		
		return build(argv[0], argv[1]);
	}

	usage(argv[0]);
}

