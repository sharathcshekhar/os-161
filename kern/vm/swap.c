int swap_out(void)
{
	int victim_indx = ppage_to_evict();
	coremap[victim_indx].pte->status = BUSY;
	off_t offset = swap_out_page(victim_indx);
	coremap[victim_indx].pte->status = SWAP_OUT;
	return coremap[victim_indx].ppage;
}

int swap_in()
{

}

int ppage_to_evict(void)
{
	int rand, tries = 0;
	if (kpages_in_use == ppages) {
		return NULL;
	}
	do {
 		rand = random() % ppages;
		tries++;
		/* Make sure we don't spin forever */
	} while ((coremap[rand].pte == NULL) && (tries < MAX_SWAP_TRIES));
	
	KASSERT(tries < MAX_SWAP_TRIES);
	return rand;
}
