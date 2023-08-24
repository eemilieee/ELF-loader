/*
 * Loader Implementation
 *
 * 2022, Operating Systems
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>	  // biblioteca pentru siginfo_t
#include <sys/mman.h> // biblioteca pentru mmap()
#include <fcntl.h>	  // biblioteca pentru open()
#include "exec_parser.h"

static so_exec_t *exec;
static void *default_handler;
static int fisier; // file descriptor-ul mentionat in pagina de manual

// signum -> codul de eroare (pentru segmentation fault este SIGSEGV)
// in campul "data" al unui segment so_seg_t memorez un vector caracteristic
// care anunta daca o pagina este mapata sau nu (contine valori de 0 sau 1)

static void segv_handler(int signum, siginfo_t *info, void *context)
{
	int i, pozitie_pagina, permisiuni = 0, flaguri = 0, cat_citesc;
	so_seg_t *segment_busit = NULL;
	char *adresa_mapata;

	// daca eroarea nu este cea pe care o tratez, se ruleaza deafult handler-ul
	if (signum != SIGSEGV)
		signal(SIGSEGV, default_handler);

	// gasesc segmentul care are probleme (un semgenation fault)
	// parcurg segmentele de memorie continute de executabil
	for (i = 0; i < exec->segments_no; i++)
	{
		// verific daca adresa la care are loc segmentation fault-ul (info->si_addr) se afla intr-unul dintre segmente
		so_seg_t *segment_curent;

		segment_curent = &(exec->segments[i]); // retin la ce segment am ajuns cu parcurgerea (mai exact adresa lui)

		// aflu daca adresa la care a avut loc segmentation fault-ul se afla in segmentul curent
		// adica daca adresa este intre adresa virtuala de memorie de unde incepe segmentul curent, in limita de memorie a lui
		if ((int)info->si_addr >= segment_curent->vaddr && (int)info->si_addr < segment_curent->vaddr + segment_curent->mem_size)
			segment_busit = segment_curent;
	}

	// cazul 1: problema nu este intr-un segment cunoscut; se actioneaza conform handler-ului default
	if (segment_busit == NULL)
		signal(SIGSEGV, default_handler);

	// cazul 3: adresa se afla intr-o pagina deja mapata a segmentului cu pricina; este un acces nepermis la memorie; se actioneaza conform handler-ului default
	// ca sa pot face asta, trebuie sa identific care pagina din segmentul busit contine adresa unde a avut loc segmentation fault-ul
	pozitie_pagina = ((int)(info->si_addr) - segment_busit->vaddr) / getpagesize();

	if (segment_busit->data != NULL)
		if (((int *)(segment_busit->data))[pozitie_pagina] == 1)
			signal(SIGSEGV, default_handler);

	// cazul 2: problema este intr-o pagina nemapata de memorie; trebuie sa fac maparea si sa copiez datele din segmentul dat din executabil la adresa de unde incepe maparea
	// ca sa pot face asta, trebuie sa identific care pagina din segmentul busit contine adresa unde a avut loc segmentation fault-ul

	pozitie_pagina = ((int)(info->si_addr) - segment_busit->vaddr) / getpagesize();

	// mapez adresa busita

	permisiuni = PROT_READ | PROT_WRITE | PROT_EXEC;
	flaguri = MAP_SHARED | MAP_ANONYMOUS | MAP_FIXED;

	// calculez cat de mult pot citi din executabil
	if ((segment_busit->file_size - pozitie_pagina * getpagesize()) > getpagesize())
		cat_citesc = getpagesize();
	else
		cat_citesc = segment_busit->file_size - pozitie_pagina * getpagesize();

	// mapez pagina
	adresa_mapata = mmap((void *)segment_busit->vaddr + pozitie_pagina * getpagesize(), getpagesize(), permisiuni, flaguri, -1, 0);

	// marchez faptul ca am mapat pagina de memorie; se creeaza un vector caracteristic (de aparitie) pentru segment cu atatea elemente cate pagini contine segmentul
	if (segment_busit->data == NULL)
		segment_busit->data = (int *)calloc(segment_busit->mem_size / getpagesize(), sizeof(int));

	((int *)(segment_busit->data))[pozitie_pagina] = 1;

	// fac citirea de date daca adresa unde trebuie sa citesc apartine fisierului; ma plasez unde trebuie in cadrul executabilului
	if (pozitie_pagina * getpagesize() < segment_busit->file_size)
	{
		lseek(fisier, segment_busit->offset + pozitie_pagina * getpagesize(), SEEK_SET);
		read(fisier, adresa_mapata, cat_citesc);
	}

	// se modifica protectia de acces asupra memoriei nou mapate
	mprotect(adresa_mapata, getpagesize(), segment_busit->perm);
}

int so_init_loader(void)
{
	int rc;
	struct sigaction sa;

	default_handler = signal(SIGSEGV, NULL);

	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = segv_handler;
	sa.sa_flags = SA_SIGINFO;
	rc = sigaction(SIGSEGV, &sa, NULL);
	if (rc < 0)
	{
		perror("sigaction");
		return -1;
	}
	return 0;
}

int so_execute(char *path, char *argv[])
{
	exec = so_parse_exec(path);

	// file-descriptorul executabilului necesar pentru citirea din acesta
	fisier = open(path, O_RDONLY, 0644);

	if (!exec)
		return -1;

	so_start_exec(exec, argv);

	return -1;
}
