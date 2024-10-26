// #include <stdio.h>
// #include <string.h>
// #include <stdbool.h>
// #include <psa/crypto.h>

// #define HARDENED_INDEX_START 0x80000000

// typedef struct {
//     uint8_t private_key[32];
//     uint8_t chain_code[32];
// } extended_key_t;

// // secp256k1曲线阶数 N
// static const uint8_t secp256k1_n[32] = {
//     0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
//     0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE,
//     0xBA, 0xAE, 0xDC, 0xE6, 0xAF, 0x48, 0xA0, 0x3B,
//     0xBF, 0xD2, 0x5E, 0x8C, 0xD0, 0x36, 0x41, 0x41
// };

// void print_hex(const char *label, const uint8_t *data, size_t len, bool reverse) {
//     printf("%s: ", label);
//     if (reverse) {
//         for (int i = len - 1; i >= 0; i--) {
//             printf("%02x", data[i]);
//         }
//     } else {
//         for (size_t i = 0; i < len; i++) {
//             printf("%02x", data[i]);
//         }
//     }
//     printf("\n");
// }

// void print_eth_private_key(const char *label, const uint8_t *key) {
//     printf("%s: 0x", label);
//     for (int i = 0; i < 32; i++) {
//         printf("%02x", key[i]);
//     }
//     printf("\n");
// }

// void hex_to_bytes(const char *hex, uint8_t *bytes, size_t len) {
//     size_t hex_len = strlen(hex);
//     const char *hex_ptr = hex;

//     if (hex_len >= 2 && hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) {
//         hex_ptr += 2;  // 跳过0x前缀
//     }

//     for (size_t i = 0; i < len; i++) {
//         sscanf(hex_ptr + (i * 2), "%2hhx", &bytes[i]);
//     }
// }

// // 比较两个字节数组
// int compare_bytes(const uint8_t *a, const uint8_t *b, size_t len) {
//     for (size_t i = 0; i < len; i++) {
//         if (a[i] < b[i]) return -1;
//         if (a[i] > b[i]) return 1;
//     }
//     return 0;
// }

// bool is_private_key_valid(const uint8_t *private_key) {
//     // 检查私钥是否为0
//     bool is_zero = true;
//     for (int i = 0; i < 32; i++) {
//         if (private_key[i] != 0) {
//             is_zero = false;
//             break;
//         }
//     }
//     if (is_zero) return false;

//     // 检查私钥是否大于等于曲线阶数
//     return compare_bytes(private_key, secp256k1_n, 32) < 0;
// }

// void add_mod_n(uint8_t *result, const uint8_t *a, const uint8_t *b) {
//     uint16_t carry = 0;
//     uint8_t temp[32];

//     // 从最低位开始相加
//     for (int i = 31; i >= 0; i--) {
//         uint16_t sum = (uint16_t)a[i] + (uint16_t)b[i] + carry;
//         temp[i] = sum & 0xFF;
//         carry = sum >> 8;
//     }

//     // 如果结果大于等于n，则减去n
//     if (carry || compare_bytes(temp, secp256k1_n, 32) >= 0) {
//         uint16_t borrow = 0;
//         for (int i = 31; i >= 0; i--) {
//             int16_t diff = (int16_t)temp[i] - (int16_t)secp256k1_n[i] - borrow;
//             result[i] = diff & 0xFF;
//             borrow = (diff < 0) ? 1 : 0;
//         }
//     } else {
//         memcpy(result, temp, 32);
//     }
// }

// psa_status_t get_public_key(const uint8_t *private_key, uint8_t *public_key) {
//     psa_status_t status;
//     psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
//     psa_key_id_t key_id;
//     size_t output_length;

//     psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_K1));
//     psa_set_key_bits(&attributes, 256);
//     psa_set_key_algorithm(&attributes, PSA_ALG_ECDSA_ANY);
//     psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_EXPORT);

//     status = psa_import_key(&attributes, private_key, 32, &key_id);
//     if (status != PSA_SUCCESS) return status;

//     status = psa_export_public_key(key_id, public_key, 65, &output_length);
//     psa_destroy_key(key_id);

//     return status;
// }

// psa_status_t derive_child_key(
//     const extended_key_t *parent,
//     uint32_t index,
//     bool is_hardened,
//     extended_key_t *child)
// {
//     psa_status_t status;
//     uint8_t data[37] = {0};
//     size_t data_length;
//     uint32_t real_index = index;

//     if (!is_private_key_valid(parent->private_key)) {
//         return PSA_ERROR_INVALID_ARGUMENT;
//     }

//     if (is_hardened) {
//         if (index >= 0x80000000) {
//             return PSA_ERROR_INVALID_ARGUMENT;
//         }
//         real_index = index + HARDENED_INDEX_START;

//         data[0] = 0x00;
//         memcpy(data + 1, parent->private_key, 32);
//         data_length = 33;
//     } else {
//         uint8_t public_key[65];
//         status = get_public_key(parent->private_key, public_key);
//         if (status != PSA_SUCCESS) {
//             return status;
//         }

//         data[0] = (public_key[64] & 1) ? 0x03 : 0x02;
//         memcpy(data + 1, public_key + 1, 32);
//         data_length = 33;
//     }

//     data[data_length++] = (real_index >> 24) & 0xFF;
//     data[data_length++] = (real_index >> 16) & 0xFF;
//     data[data_length++] = (real_index >> 8) & 0xFF;
//     data[data_length++] = real_index & 0xFF;

//     psa_key_attributes_t hmac_attrs = PSA_KEY_ATTRIBUTES_INIT;
//     psa_key_id_t hmac_key;
//     uint8_t hmac_output[64];
//     size_t hmac_length;

//     psa_set_key_type(&hmac_attrs, PSA_KEY_TYPE_HMAC);
//     psa_set_key_bits(&hmac_attrs, 256);
//     psa_set_key_algorithm(&hmac_attrs, PSA_ALG_HMAC(PSA_ALG_SHA_512));
//     psa_set_key_usage_flags(&hmac_attrs, PSA_KEY_USAGE_SIGN_MESSAGE);

//     status = psa_import_key(&hmac_attrs, parent->chain_code, 32, &hmac_key);
//     if (status != PSA_SUCCESS) {
//         return status;
//     }

//     status = psa_mac_compute(hmac_key, PSA_ALG_HMAC(PSA_ALG_SHA_512),
//                            data, data_length,
//                            hmac_output, sizeof(hmac_output), &hmac_length);
//     psa_destroy_key(hmac_key);

//     if (status != PSA_SUCCESS) {
//         return status;
//     }

//     if (compare_bytes(hmac_output, secp256k1_n, 32) >= 0) {
//         return PSA_ERROR_INVALID_ARGUMENT;
//     }

//     memcpy(child->chain_code, hmac_output + 32, 32);
//     add_mod_n(child->private_key, hmac_output, parent->private_key);

//     if (!is_private_key_valid(child->private_key)) {
//         return PSA_ERROR_INVALID_ARGUMENT;
//     }

//     return PSA_SUCCESS;
// }

// int main() {
//     psa_status_t status = psa_crypto_init();
//     if (status != PSA_SUCCESS) {
//         printf("PSA初始化失败\n");
//         return 1;
//     }

//     extended_key_t master_key;
//     hex_to_bytes(
//         "824f3e64c631d4f2a98c9fcf43e94dc40f8a783f5bda6657ce5cbf6f845c7d39",
//         master_key.private_key,
//         32
//     );
//     hex_to_bytes(
//         "33b081a4bdf01607df10300ce24cf4cb75491085b8b5af6d6de8995d8368fc11",
//         master_key.chain_code,
//         32
//     );

//     print_eth_private_key("主私钥", master_key.private_key);
//     print_hex("主链码", master_key.chain_code, 32, false);

//     extended_key_t current_key = master_key;
//     uint32_t path[] = {44, 60, 0, 0, 1};
//     bool is_hardened[] = {true, true, true, true, false};

//     for (int i = 0; i < 5; i++) {
//         extended_key_t child_key;
//         status = derive_child_key(&current_key, path[i], is_hardened[i], &child_key);
//         if (status != PSA_SUCCESS) {
//             printf("派生失败在步骤 %d, status: %d\n", i + 1, status);
//             return 1;
//         }
//         current_key = child_key;
//     }

//     print_eth_private_key("最终私钥", current_key.private_key);
//     print_hex("最终链码", current_key.chain_code, 32, false);
//     printf("\n\n\n\n\n\n");
//     return 0;
// }
