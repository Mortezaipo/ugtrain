/* discovery.cpp:    discover dynamic memory objects
 *
 * Copyright (c) 2013, by:      Sebastian Riemer
 *    All rights reserved.      Ernst-Reinke-Str. 23
 *                              10369 Berlin, Germany
 *                             <sebastian.riemer@gmx.de>
 *
 * powered by the Open Game Cheating Association
 *
 * This file may be used subject to the terms and conditions of the
 * GNU General Public License Version 2, or any later version
 * at your option, as published by the Free Software Foundation.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <vector>
#include <cstring>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include "common.h"
#include "commont.cpp"
#include "getch.h"
#include "cfgparser.h"
#include "fifoparser.h"
#include "memattach.h"
#include "system.h"
#include "discovery.h"

#define DYNMEM_FILE "/tmp/memhack_file"
#define MAX_BT 10


static i32 postproc_stage5 (struct app_options *opt, list<CfgEntry> *cfg,
			    string *cfg_path, vector<string> *lines)
{
	list<CfgEntry>::iterator cfg_it;
	DynMemEntry *tmp = NULL;
	u8 discovered = 0;
	u32 lnr;
	char ch;

	for (cfg_it = cfg->begin(); cfg_it != cfg->end(); cfg_it++) {
		if (!cfg_it->dynmem)
			continue;
		if (cfg_it->dynmem->adp_addr == opt->disc_addr &&
		    cfg_it->dynmem->adp_stack) {
			discovered = 1;
			continue;
		}
		if (!(cfg_it->dynmem->adp_addr &&
		      cfg_it->dynmem->adp_stack)) {
			cout << "Undiscovered objects found!" << endl;
			if (discovered) {
				cout << "Next discovery run (y/n)? : ";
				fflush(stdout);
				ch = 'n';
				ch = do_getch();
				cout << ch << endl;
				if (ch == 'y') {
					opt->disc_str[1] = '\0';
					return DISC_NEXT;
				}
			}
			cerr << "Discovery failed!" << endl;
			exit(-1);
		}
	}
	cout << "Discovery successful!" << endl;

	// Take over discovery
	for (cfg_it = cfg->begin(); cfg_it != cfg->end(); cfg_it++) {
		if (!cfg_it->dynmem || cfg_it->dynmem == tmp)
			continue;
		tmp = cfg_it->dynmem;
		cout << "Obj. " << tmp->name
		     << ", old_code: " << hex << tmp->code_addr
		     << ", new_code: " << tmp->adp_addr << dec << endl;
		cfg_it->dynmem->code_addr = tmp->adp_addr;
		cout << "Obj. " << tmp->name
		     << ", old_offs: " << hex << tmp->stack_offs
		     << ", new_offs: " << tmp->adp_stack << dec << endl;
		tmp->stack_offs = tmp->adp_stack;
		lnr = tmp->cfg_line;
		lines->at(lnr) = "dynmemstart " + tmp->name + " "
			+ to_string(tmp->mem_size) + " "
			+ to_string(tmp->code_addr) + " "
			+ to_string(tmp->stack_offs);
	}
	// Adaption isn't required anymore
	lnr = opt->adp_req_line;
	if (lnr > 0)
		lines->at(lnr) = "adapt_required 0";

	// Write back config
	cout << "Writing back config.." << endl;
	write_config_vect(cfg_path, lines);

	// Run game with libmemhack
	opt->do_adapt = 0;
	opt->disc_str = NULL;
	use_libmemhack(opt);
	return DISC_OKAY;
}

// mf() callback for read_dynmem_buf()
static void process_disc1234_malloc (list<CfgEntry> *cfg,
				     struct post_parse *pp,
				     void *mem_addr,
				     ssize_t mem_size,
				     void *code_addr,
				     void *stack_offs)
{
	void *in_addr = pp->argp;
	void *codes[MAX_BT] = { NULL };
	void *soffs[MAX_BT] = { NULL };
	char *sep_pos;
	int i, ret, num_codes = 0, num_soffs = 0;
	u8 is_stage4 = 0;

	if (in_addr >= mem_addr &&
	    in_addr < PTR_ADD(void *,mem_addr, mem_size)) {
		cout << "m" << mem_addr << ";" << "s" << mem_size
		     << " contains " << in_addr << ", offs: "
		     << PTR_SUB(void *, in_addr, mem_addr) << endl;

		/* stage 3 and 4 parsing */
		for (i = 0; i < MAX_BT; i++) {
			sep_pos = strchr(pp->ibuf + pp->ppos, ';');
			if (!sep_pos)
				break;
			pp->ppos = sep_pos + 1 - pp->ibuf;
			if (is_stage4) {
				sep_pos = strchr(pp->ibuf + pp->ppos, ';');
				if (!sep_pos)
					break;
				pp->ppos = sep_pos + 1 - pp->ibuf;
			}
			ret = sscanf(pp->ibuf + pp->ppos, "c%p;o%p",
				     &codes[i], &soffs[i]);
			if (ret == 2) {
				is_stage4 = 1;
				num_codes++;
				num_soffs++;
			} else if (ret == 1) {
				if (is_stage4)
					break;
				num_codes++;
			} else {
				break;
			}
		}

		/* stage 3 and 4 post-processing */
		for (i = 0; i < num_codes; i++) {
			cout << "c" << codes[i];
			if (is_stage4)
				cout << ";o" << soffs[i];
			//TODO get and print function call from disassembly
			cout << endl;
		}
	}
	//else
		//cout << "m" << mem_addr << ";" << "s" << mem_size << endl;
}

// ff() callback for read_dynmem_buf()
static void process_disc1_free (list<CfgEntry> *cfg,
				void *argp,
				void *mem_addr)
{
	//cout << "f" << mem_addr << endl;
}

static i32 postproc_stage1 (struct app_options *opt, list<CfgEntry> *cfg)
{
	i32 ifd, pmask = PARSE_M | PARSE_S;
	void *mem_addr;

	ifd = open(DYNMEM_FILE, O_RDONLY);
	if (ifd < 0) {
		perror("open ifd");
		return -1;
	}

	restore_getch();

	cout << "Memory address (e.g. 0xdeadbeef): ";
	fflush(stdout);
	cin >> hex >> mem_addr;
	if (mem_addr == NULL) {
		cerr << "Error: Invalid memory address!" << endl;
		return -1;
	}
	cout << hex << "Searing for " << mem_addr << dec
	     << " in discovery output.." << endl;

	while (1) {
		if (read_dynmem_buf(cfg, mem_addr, ifd, pmask,
		    process_disc1234_malloc, process_disc1_free))
			break;
	}

	if (prepare_getch() != 0) {
		cerr << "Error while terminal preparation!" << endl;
		return -1;
	}

	return 0;
}

i32 postproc_discovery (struct app_options *opt, list<CfgEntry> *cfg,
			string *cfg_path, vector<string> *lines)
{
	if (opt->disc_str[0] >= '1' && opt->disc_str[0] <= '4')
		return postproc_stage1(opt, cfg);
	if (opt->disc_str[0] != '5')
		exit(0);
	return postproc_stage5(opt, cfg, cfg_path, lines);
}

// mf() callback for read_dynmem_buf()
static void process_disc5_output (list<CfgEntry> *cfg,
				  struct post_parse *pp,
				  void *mem_addr,
				  ssize_t mem_size,
				  void *code_addr,
				  void *stack_offs)
{
	list<CfgEntry>::iterator it;

	cout << "Discovery output: " << endl;
	cout << "m" << hex << mem_addr << dec << ";s"
	     << mem_size << hex << ";c" << code_addr
	     << ";o" << stack_offs << dec << endl;

	// find object and set adp_stack
	for (it = cfg->begin(); it != cfg->end(); it++) {
		if (it->dynmem &&
		    it->dynmem->adp_addr == code_addr) {
			it->dynmem->adp_stack = stack_offs;
			break;
		}
	}
}

void run_stage5_loop (list<CfgEntry> *cfg, i32 ifd, i32 pmask, pid_t pid)
{
	while (1) {
		sleep(1);
		read_dynmem_buf(cfg, NULL, ifd, pmask, process_disc5_output,
				NULL);
		if (memattach_test(pid) != 0) {
			cerr << "PTRACE ERROR PID[" << pid << "]!"
			     << endl;
			break;
		}
	}
}

void run_stage1234_loop (void *argp)
{
	i32 ifd = *(i32 *) argp;
	i32 ofd;
	char buf[PIPE_BUF];
	ssize_t rbytes, wbytes;

	ofd = open(DYNMEM_FILE, O_WRONLY | O_CREAT | O_TRUNC,
		   S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	if (ofd < 0) {
		perror("open ofd");
		exit(1);
	}

	while (1) {
		rbytes = read(ifd, buf, sizeof(buf));
		if (rbytes == 0 || (rbytes < 0 && errno == EAGAIN))
			continue;
		if (rbytes < 0) {
			perror("read");
			break;
		}
		wbytes = write(ofd, buf, rbytes);
		if (wbytes != rbytes) {
			perror("write");
			break;
		}
	}
}

i32 prepare_discovery (struct app_options *opt, list<CfgEntry> *cfg)
{
	string disc_str, cmd_str;
	int i, ret, found = 0;
	list<CfgEntry>::iterator it;
	void *heap_soffs, *heap_eoffs, *bt_saddr, *bt_eaddr;
	size_t mem_size;
	char pbuf[PIPE_BUF] = { 0 };

	if (!opt->disc_str)
		return 0;

	switch (opt->disc_str[0]) {
	case '0':
		// just get stack end and heap start
		opt->disc_str = (char *) "0";
		break;
	case '1':
	case '2':
		if (strlen(opt->disc_str) == 1) {
			disc_str = opt->disc_str[0];
			disc_str += ";0x0;0x0;0";
			opt->disc_str = to_c_str(&disc_str);
		}
		cout << "disc_str: " << opt->disc_str << endl;
		break;
	case '3':
	case '4':
		ret = sscanf(&opt->disc_str[1], ";%p;%p;%zd;%p;%p", &heap_soffs,
			     &heap_eoffs, &mem_size, &bt_saddr, &bt_eaddr);
		if (ret < 3) {
			cerr << "Error: Not enough arguments for discovery "
				"stage " << opt->disc_str[0] << "!" << endl;
			cerr << "Use at least \'" << opt->disc_str[0]
			     << ";0x0;0x0;<size>\'" << endl;
			goto err;
		}
		if (mem_size == 0) {
			cerr << "Error: Too much data! Please discover the "
				"size first!" << endl;
			goto err;
		}
		if (ret < 5) {
			cmd_str = "objdump -p `which ";
			cmd_str += opt->proc_name;
			cmd_str += "` | grep \"INIT\\|FINI\" "
				   "| tr -d [:upper:] | tr -d [:blank:]";
			cout << "$ " << cmd_str << endl;
			if (run_cmd_pipe(cmd_str.c_str(), NULL, pbuf,
			    sizeof(pbuf), 1) <= 0)
				goto err;
			if (sscanf(pbuf, "%p\n%p", &bt_saddr, &bt_eaddr) != 2) {
				goto err;
			} else {
				disc_str = opt->disc_str[0];
				disc_str += ";";
				disc_str += to_string(heap_soffs);
				disc_str += ";";
				disc_str += to_string(heap_eoffs);
				disc_str += ";";
				disc_str += to_string(mem_size);
				disc_str += ";";
				disc_str += to_string(bt_saddr);
				disc_str += ";";
				disc_str += to_string(bt_eaddr);
				opt->disc_str = to_c_str(&disc_str);
			}
		}
		cout << "disc_str: " << opt->disc_str << endl;
		break;
	case '5':
		if (!opt->do_adapt) {
			for (it = cfg->begin(); it != cfg->end(); it++) {
				if (it->dynmem && !it->dynmem->adp_addr)
					it->dynmem->adp_addr = it->dynmem->code_addr;
			}
		}
		if (strlen(opt->disc_str) != 1) {
			cerr << "Sorry, but this stage is reserved for "
				"auto-adaption!" << endl;
			goto err;
		} else {
			for (it = cfg->begin(); it != cfg->end(); it++) {
				if (it->dynmem && it->dynmem->adp_addr &&
				    !it->dynmem->adp_stack) {
					opt->disc_addr = it->dynmem->adp_addr;
					found = 1;
					break;
				}
			}
			if (!found)
				goto err;

			disc_str = opt->disc_str[0];
			disc_str += ";0x0;0x0;";
			disc_str += to_string(it->dynmem->mem_size);
			for (i = 0; i < 3; i++) {
				disc_str += ";";
				disc_str += to_string(it->dynmem->adp_addr);
			}
			opt->disc_str = to_c_str(&disc_str);
			cout << "Discovering object " << it->dynmem->name
			     << "." << endl
			     << "Please ensure that it gets allocated!" << endl;
			cout << "disc_str: " << opt->disc_str << endl;
		}
		break;
	default:
		goto err;
	}

	return 0;
err:
	cerr << "Error while preparing discovery!" << endl;
	return -1;
}