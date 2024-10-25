#ifndef __BIP32_H__
#define __BIP32_H__

int hd_node_from_seed(const uint8_t *seed, int seed_len,
		      uint8_t *master_sk, uint8_t *chain_code);
bool generate_xprv(const uint8_t *master_key, const uint8_t *chain_code, char *xprv_out, size_t *xprv_size);
#endif
