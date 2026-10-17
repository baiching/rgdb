#ifndef PG_MGR_H
#define PG_MGR_H

#include <stdint.h>

typedef struct PageHeader PageHeader;
typedef struct PageCache PageCache;
typedef struct RgdbPage RgdbPage;

/* Every page should hold this structure */
struct PageHeader {
	PageCache* pageCache;	/* Owner of this page */
	uint32_t pageId;		/* this is temporary */
	void* pageData;			/* page data */
	void* pageExtras;		/* Extra contents */

	PageHeader* pageDirty;	/* the list of dirty pages */

	uint64_t pageHash;		/* hash for the page */

	uint32_t flags;				/* bitwise flags */

	uint32_t numOfUsersRef;	/* Number of users of the page */
	PageHeader* pageDirtyNext;
	PageHeader* pageDirtyPrev;

};

#define PAGEHEADER_WRITABLE		0x001	/* Journaled amd ready to modify */
#define PAGEHEADER_NEED_SYNC	0x002	/* Need to Fsync this page before writing */

#define PAGEHEADER_DONT_WRITE	0x004	/* Do not write this page to disk */
#define PAGEHEADER_WAL_APPEND	0x008	/* Append to WAL */

#define PAGEHEADER_DIRY			0x010	/* page is in dirty list */
#define PAGEHEADER_CLEAN		0x020	/* page is not in dirty list */
#define PAGEHEADER_PINNED		0x040	/* page is in use */
#define PAGEHEADER_UNPINNED		0x080	/* page is not in use */

/* Initilize and shut down page cache entirely */
int rgdb_pageCache_initilize(void);
void rgdb_pageCache_shutDown(void);

void rgdb_pageCache_bufferSetup(int size, int n);

int rgdb_pageCache_newPageCache(
	uint32_t pageSize,
	uint32_t extraSize,
	int bPurgable
)

/* modify the page size after cacche been created */
int rgdb_pageCache_setPageSize(PageCache *, int);

/* returns cache size */
int rgdb_pageCache_size(void);

/* page related operations */
RgdbPage *rgdb_pageCache_fetch(PageCache *, uint32_t pageid, int flag); /* get the page */
PageHeader *rgdb_pageCache_finishFetch(PageCache*, uint32_t pageid, RgdbPage *page); 
void rgdb_pageCache_releasePage(PageHeader*); /* unpinned */

#endif // !PG_MRG_H

