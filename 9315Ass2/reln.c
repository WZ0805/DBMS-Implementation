// reln.c ... functions on Relations
// part of signature indexed files
// Written by John Shepherd, March 2019

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include "defs.h"
#include "reln.h"
#include "page.h"
#include "tuple.h"
#include "tsig.h"
#include "bits.h"
#include "hash.h"
#include "psig.h"

// open a file with a specified suffix
// - always open for both reading and writing

File openFile(char *name, char *suffix)
{
	char fname[MAXFILENAME];
	sprintf(fname,"%s.%s",name,suffix);
	File f = open(fname,O_RDWR|O_CREAT,0644);
	assert(f >= 0);
	return f;
}

// create a new relation (five files)
// data file has one empty data page

Status newRelation(char *name, Count nattrs, float pF, char sigtype,
                   Count tk, Count tm, Count pm, Count bm)
{
	Reln r = malloc(sizeof(RelnRep));
	RelnParams *p = &(r->params);
	assert(r != NULL);
	p->nattrs = nattrs;
	p->pF = pF,
	p->sigtype = sigtype;
	p->tupsize = 28 + 7*(nattrs-2);
	Count available = (PAGESIZE-sizeof(Count));
	p->tupPP = available/p->tupsize;
	p->tk = tk; 
	if (tm%8 > 0) tm += 8-(tm%8); // round up to byte size
	p->tm = tm; p->tsigSize = tm/8; p->tsigPP = available/(tm/8);
	if (pm%8 > 0) pm += 8-(pm%8); // round up to byte size
	p->pm = pm; p->psigSize = pm/8; p->psigPP = available/(pm/8);
	if (p->psigPP < 2) { free(r); return -1; }
	if (bm%8 > 0) bm += 8-(bm%8); // round up to byte size
	p->bm = bm; p->bsigSize = bm/8; p->bsigPP = available/(bm/8);
	if (p->bsigPP < 2) { free(r); return -1; }
	r->infof = openFile(name,"info");
	r->dataf = openFile(name,"data");
	r->tsigf = openFile(name,"tsig");
	r->psigf = openFile(name,"psig");
	r->bsigf = openFile(name,"bsig");
	addPage(r->dataf); p->npages = 1; p->ntups = 0;
	addPage(r->tsigf); p->tsigNpages = 1; p->ntsigs = 0;
	addPage(r->psigf); p->psigNpages = 1; p->npsigs = 0;
//	addPage(r->bsigf); p->bsigNpages = 1; p->nbsigs = 0; // replace this
	// Create a file containing "pm" all-zeroes bit-strings,
    // each of which has length "bm" bits
	//TODO
    p->bsigNpages = 0;p->nbsigs = 0;//since we know pm bm and maxbsigpp, we can set all pages ready at start
    int need_pages=psigBits(r)/maxBsigsPP(r)+1;//calculate how many pages we need
    for(int i=0;i<need_pages;i++){
        Page current_page = newPage();
        if(current_page==NULL) return NO_PAGE;//judge if create new page successfully
        addPage(r->bsigf);
        if(i!=need_pages-1){//that means this page is not the last one page
            for(int j=0;j<maxBsigsPP(r);j++){
                Bits bsig = newBits(bsigBits(r));
                putBits(current_page,j,bsig);
                p->nbsigs++;
                addOneItem(current_page);
                freeBits(bsig);
            }
            putPage(r->bsigf,i,current_page);
        }
        else{//if page is the last one page
            int off_set=psigBits(r)%maxBsigsPP(r);
            for(int j=0;j<off_set;j++){
                Bits bsig = newBits(bsigBits(r));
                putBits(current_page,j,bsig);
                p->nbsigs++;
                addOneItem(current_page);
                freeBits(bsig);
            }
            putPage(r->bsigf,i,current_page);
        }
        p->bsigNpages++;
    }
	closeRelation(r);
	return 0;
}

// check whether a relation already exists

Bool existsRelation(char *name)
{
	char fname[MAXFILENAME];
	sprintf(fname,"%s.info",name);
	File f = open(fname,O_RDONLY);
	if (f < 0)
		return FALSE;
	else {
		close(f);
		return TRUE;
	}
}

// set up a relation descriptor from relation name
// open files, reads information from rel.info

Reln openRelation(char *name)
{
	Reln r = malloc(sizeof(RelnRep));
	assert(r != NULL);
	r->infof = openFile(name,"info");
	r->dataf = openFile(name,"data");
	r->tsigf = openFile(name,"tsig");
	r->psigf = openFile(name,"psig");
	r->bsigf = openFile(name,"bsig");
	read(r->infof, &(r->params), sizeof(RelnParams));
	return r;
}

// release files and descriptor for an open relation
// copy latest information to .info file
// note: we don't write ChoiceVector since it doesn't change

void closeRelation(Reln r)
{
	// make sure updated global data is put in info file
	lseek(r->infof, 0, SEEK_SET);
	int n = write(r->infof, &(r->params), sizeof(RelnParams));
	assert(n == sizeof(RelnParams));
	close(r->infof); close(r->dataf);
	close(r->tsigf); close(r->psigf); close(r->bsigf);
	free(r);
}

// insert a new tuple into a relation
// returns page where inserted
// returns NO_PAGE if insert fails completely

PageID addToRelation(Reln r, Tuple t)
{
	assert(r != NULL && t != NULL && strlen(t) == tupSize(r));
	Page p;  PageID pid;
	RelnParams *rp = &(r->params);
	
	// add tuple to last page
	pid = rp->npages-1;
	p = getPage(r->dataf, pid);
	// check if room on last page; if not add new page
	if (pageNitems(p) == rp->tupPP) {
		addPage(r->dataf);
		rp->npages++;
		pid++;
		free(p);
		p = newPage();
		if (p == NULL) return NO_PAGE;
	}
	addTupleToPage(r, p, t);
	rp->ntups++;  //written to disk in closeRelation()
	putPage(r->dataf, pid, p);

	// compute tuple signature and add to tsigf
	
	//TODO
    Bits tuple_sig = makeTupleSig(r,t);
    PageID tsig_pid = nTsigPages(r) - 1;
    Page tuple_page = getPage(tsigFile(r),tsig_pid);
    if(pageNitems(tuple_page) == maxTsigsPP(r)){
        free(tuple_page);
        tuple_page=newPage();
        if(tuple_page==NULL) return NO_PAGE;//judge if the new page created successfully
        addPage(tsigFile(r));               //add a new page to file
        nTsigPages(r)++;                    //count how many tuple sig pages add one
        tsig_pid=nTsigPages(r)-1;           //calculate the new tuple signature page id of this new page
        putBits(tuple_page,0,tuple_sig);
        addOneItem(tuple_page);             // the number of items of this page add one
        nTsigs(r)++;                        //the number of tuplge signatures of this relation add one
        putPage(tsigFile(r),tsig_pid,tuple_page);//write the page into the file
    }else{                                  //else means we dont need to create a new page
        putBits(tuple_page,pageNitems(tuple_page),tuple_sig);//write the tuple signature into the page
        addOneItem(tuple_page);             //the number of items of this page add one
        nTsigs(r)++;                        //the number of tuplge signatures of this relation add one
        putPage(tsigFile(r),tsig_pid,tuple_page);//write the page into the file
    }freeBits(tuple_sig);

	// compute page signature and add to psigf

	//TODO
    Bits Psig = makePageSig(r, t);
    PageID psig_pid = nPsigPages(r)-1;
    Page psig_page=getPage(psigFile(r), psig_pid);
    if(nPages(r)!=nPsigs(r)){                       //if the number data page not equal to n psigs, means add new data page,then need add new psig
        if(pageNitems(psig_page)!=maxPsigsPP(r)){   //if this page is not full
            putBits(psig_page,pageNitems(psig_page),Psig);//write the psig to the page
            addOneItem(psig_page);
            nPsigs(r)++;
        }else if(pageNitems(psig_page)==maxPsigsPP(r)){//if page is full
            free(psig_page);
            psig_page=newPage();
            if(psig_page==NULL) return NO_PAGE;         //judge if the new page created successfully
            addPage(psigFile(r));
            nPsigPages(r)++;
            psig_pid = nPsigPages(r)-1;
            putBits(psig_page,pageNitems(psig_page),Psig);
            addOneItem(psig_page);
            nPsigs(r)++;
        }
    }else{                                      //that means dont add new page on datafile, so we need to or the last one psig
        Bits last_psig = newBits(psigBits(r));
        getBits(psig_page,pageNitems(psig_page)-1,last_psig);
        orBits(Psig,last_psig);
        putBits(psig_page,pageNitems(psig_page)-1,Psig);
        freeBits(last_psig);
    }putPage(psigFile(r),psig_pid,psig_page);
    freeBits(Psig);

    // use page signature to update bit-slices
    //TODO
    PageID bsig_pid;
    Offset off_set;
    Bits psig = makePageSig(r,t);
    for(int pbit=0;pbit<psigBits(r);pbit++){
        bsig_pid=pbit/maxBsigsPP(r);
        off_set = pbit%maxBsigsPP(r);
        if(bitIsSet(psig,pbit)){
            Page bsig_page = getPage(bsigFile(r),bsig_pid);
            Bits bsig = newBits(bsigBits(r));
            getBits(bsig_page,off_set,bsig);
            setBit(bsig,nPsigs(r)-1);
            putBits(bsig_page,off_set,bsig);
            putPage(bsigFile(r),bsig_pid,bsig_page);
            freeBits(bsig);
        }
    }freeBits(psig);
    return nPages(r)-1;
}

// displays info about open Reln (for debugging)

void relationStats(Reln r)
{
	RelnParams *p = &(r->params);
	printf("Global Info:\n");
	printf("Dynamic:\n");
    printf("  #items:  tuples: %d  tsigs: %d  psigs: %d  bsigs: %d\n",
			p->ntups, p->ntsigs, p->npsigs, p->nbsigs);
    printf("  #pages:  tuples: %d  tsigs: %d  psigs: %d  bsigs: %d\n",
			p->npages, p->tsigNpages, p->psigNpages, p->bsigNpages);
	printf("Static:\n");
    printf("  tups   #attrs: %d  size: %d bytes  max/page: %d\n",
			p->nattrs, p->tupsize, p->tupPP);
	printf("  sigs   %s",
            p->sigtype == 'c' ? "catc" : "simc");
    if (p->sigtype == 's')
	    printf("  bits/attr: %d", p->tk);
    printf("\n");
	printf("  tsigs  size: %d bits (%d bytes)  max/page: %d\n",
			p->tm, p->tsigSize, p->tsigPP);
	printf("  psigs  size: %d bits (%d bytes)  max/page: %d\n",
			p->pm, p->psigSize, p->psigPP);
	printf("  bsigs  size: %d bits (%d bytes)  max/page: %d\n",
			p->bm, p->bsigSize, p->bsigPP);
}
