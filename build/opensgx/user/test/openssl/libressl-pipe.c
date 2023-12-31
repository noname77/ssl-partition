#include "libressl-pipe.h"

#define lh_SGX_SESSION_new() LHM_lh_new(SGX_SESSION, sgx_session)
#define lh_SGX_SESSION_insert(lh, inst) LHM_lh_insert(SGX_SESSION, lh, inst)
#define lh_SGX_SESSION_retrieve(lh,inst) LHM_lh_retrieve(SGX_SESSION, lh, inst)
#define lh_SGX_SESSION_delete(lh, inst) LHM_lh_delete(SGX_SESSION, lh, inst)

static int
sgx_session_cmp(const SGX_SESSION *a, const SGX_SESSION *b)
{
  return strncmp((char *) a->id, (char *) b->id, SGX_SESSION_ID_LENGTH);
}

static IMPLEMENT_LHASH_COMP_FN(sgx_session, SGX_SESSION)

static unsigned long
sgx_session_hash(const SGX_SESSION *a)
{
  unsigned char b[SGX_SESSION_ID_LENGTH];
  MD5(a->id, SGX_SESSION_ID_LENGTH, b);

  return(b[0]|(b[1]<<8)|(b[2]<<16)|(b[3]<<24));
}

static IMPLEMENT_LHASH_HASH_FN(sgx_session, SGX_SESSION)

int cmd_counter = 0;
EVP_PKEY* private_key = NULL;
RSA* rsa = NULL;
SSL_CTX* ctx = NULL;
cmd_t _commands[MAX_COMMANDS];

LHASH_OF(SGX_SESSION) *sgx_sess_lh;
LHASH_OF(SGX_SESSION) *ssl_sess_lh;
SGX_SESSION *sgx_sess;

// TODO: make it uniform with the script (crt / cert)
// has to be the same file you use for nginx
// char priv_key_file[] = "/etc/nginx/ssl/nginx.key";
// char cert_file[] = "/etc/nginx/ssl/nginx.cert";
char priv_key_file[] = "/usr/local/nginx/conf/nginx.key";
char cert_file[] = "/usr/local/nginx/conf/nginx.cert";

/* main operation. communicate with tor-gencert & tor process */
void
enclave_main(int argc, char** argv)
{
  if (argc != 2) {
    debug_printf("Usage: ./test.sh test/openssl/libressl-pipe\n");
    sgx_exit(NULL);
  }

  // initialize the ssl library
  debug_fprintf(stdout, "Initialising SSL library and loading error strings...");
  SSL_library_init();
  SSL_load_error_strings();
  debug_fprintf(stdout, "Done\n");

  debug_fprintf(stdout, "Initializing SGX & SSL SESSION lhash...");
  if ((sgx_sess_lh = lh_SGX_SESSION_new()) == NULL ||
       (ssl_sess_lh = lh_SGX_SESSION_new()) == NULL)
          sgx_exit(NULL);
  debug_fprintf(stdout, "Done\n");

  /* Load Private Key and certificate to SSL_CTX structure */
  load_pKey_and_cert_to_ssl_ctx();

  /* initialize the commnads */
  debug_fprintf(stdout, "Registering commands...");
  register_commands();
  debug_fprintf(stdout, "Done\n");

  // pipe read loop:
  //   -> fetch in command_len -> command -> data_len -> data
  //   -> call the appriopriate command function
  while (1) {
    run_command_loop();
  }
}

void
init_session(SGX_SESSION *sgx_s)
{
  sgx_s->s = SSL_new(ctx);
  ssl_get_new_session(sgx_s->s, 1);
}

// just some debug output
void
print_session_params(SSL* s)
{
  debug_printf("client_random:\n");
  print_hex(sgx_sess->s->s3->client_random, SSL3_RANDOM_SIZE);
  debug_printf("server_random:\n");
  print_hex(sgx_sess->s->s3->server_random, SSL3_RANDOM_SIZE);
  debug_printf("master_key:\n");
  print_hex(sgx_sess->s->session->master_key, SSL3_MASTER_SECRET_SIZE);
}

// TODO: should the ctx be a parameter? other stuff?
void
load_pKey_and_cert_to_ssl_ctx()
{
  debug_fprintf(stdout, "Creating SSL context...");
  ctx = SSL_CTX_new(SSLv23_method());
  if (!ctx) {
    puts(" Context creation failed");
    sgx_exit(NULL);
  }
  debug_fprintf(stdout, "Done\n");

  debug_fprintf(stdout, "Loading SSL context Certificate...");
  /* Load the server certificate into the SSL_CTX structure */
  if (SSL_CTX_use_certificate_file(ctx, cert_file, SSL_FILETYPE_PEM) <= 0) {
    debug_fprintf(stderr, "Context certificate file failed\n");
    sgx_exit(NULL);
  }
  debug_fprintf(stdout, "Done\n");

  /* Load the private-key corresponding to the server certificate */
  debug_fprintf(stdout, "Loading SSL context Private Key...");
  if (SSL_CTX_use_PrivateKey_file(ctx, priv_key_file, SSL_FILETYPE_PEM) <= 0) {
    debug_fprintf(stderr, "Context Private Key failed\n");
    sgx_exit(NULL);
  }
  debug_fprintf(stdout, "Done\n");

  debug_fprintf(stdout, "Retrieving Private Key from SSL context...");
  if((private_key = SSL_CTX_get_privatekey(ctx)) == NULL){
    debug_fprintf(stderr, "Retrieving Private Key from ctx failed\n");
    sgx_exit(NULL);
  }
  debug_fprintf(stdout, "Done\n");

  rsa = private_key->pkey.rsa;
}

// initialize the commnd array
void
register_commands()
{
  register_command(CMD_CLNT_RAND, cmd_clnt_rand);
  register_command(CMD_SRV_RAND, cmd_srv_rand);
  register_command(CMD_PREMASTER, cmd_premaster);
  register_command(CMD_MASTER_SEC, cmd_master_sec);
  register_command(CMD_RSA_SIGN, cmd_rsa_sign);
  register_command(CMD_RSA_SIGN_SIG_ALG, cmd_rsa_sign_sig_alg);
  register_command(CMD_GET_ECDHE_PUBLIC_PARAM, cmd_ecdhe_get_public_param);
  register_command(CMD_GET_ECDHE_PRE_MASTER, cmd_ecdhe_generate_pre_master);
  register_command(CMD_SSL_HANDSHAKE_DONE, cmd_ssl_handshake_done);
  register_command(CMD_SSL_SESSION_REMOVE, cmd_ssl_session_remove);
  
  // #ifdef OPENSSL_WITH_SGX_KEYBLOCK
  register_command(CMD_KEY_BLOCK, cmd_key_block);
  register_command(CMD_FINAL_FINISH_MAC, cmd_final_finish_mac);
  register_command(CMD_CHANGE_CIPHER_STATE, cmd_change_cipher_state);
  register_command(CMD_SGX_TLS1_ENC, cmd_sgx_tls1_enc);
  // #endif

}

// needs to be called before the command can be used
void
register_command(int cmd, void (*callback)(cmd_pkt_t, unsigned char*))
{
  // just add it to our static array.
  if (cmd < MAX_COMMANDS) {
    _commands[cmd].cmd_num = cmd;
    _commands[cmd].callback = callback;
  } else {
    // TODO: error, too many commands
    debug_printf("ERROR: command array full, increase MAX_COMMANDS\n");
  }
}

// tries to match incoming command to a registered one, executes if found
void
check_commands(cmd_pkt_t cmd_pkt, unsigned char* data)
{
  if(cmd_pkt.cmd == _commands[cmd_pkt.cmd].cmd_num){

    SGX_SESSION sgx_s, ssl_s, *sgx_sp;
    memcpy(sgx_s.id, cmd_pkt.sgx_session_id, SGX_SESSION_ID_LENGTH);
    memcpy(ssl_s.id, cmd_pkt.ssl_session_id, SSL3_SSL_SESSION_ID_LENGTH);

    debug_fprintf(stdout, "SGX session id: ");
    print_hex(sgx_s.id, SGX_SESSION_ID_LENGTH);
    debug_fprintf(stdout, "SSL session id: ");
    print_hex(ssl_s.id, SSL3_SSL_SESSION_ID_LENGTH);

    if((sgx_sp = lh_SGX_SESSION_retrieve(ssl_sess_lh, &ssl_s)) == NULL){
      debug_fprintf(stdout, "SSL session cache MISS\n");

      if((sgx_sp = lh_SGX_SESSION_retrieve(sgx_sess_lh, &sgx_s)) == NULL){
        debug_fprintf(stdout, "SGX session cache MISS\n");
        if((sgx_sp = calloc(sizeof(SGX_SESSION), 1)) == NULL){
          debug_fprintf(stderr, "sgx_sp calloc() failed: %s\n", strerror(errno));
          sgx_exit(NULL);
        }
        memcpy(sgx_sp->id, sgx_s.id, SGX_SESSION_ID_LENGTH);
        sgx_sp->type = SGX_SESSION_TYPE;

        lh_SGX_SESSION_insert(sgx_sess_lh, sgx_sp);

        debug_fprintf(stdout, "Initializing SGX session...");
        init_session(sgx_sp);
        debug_fprintf(stdout, "Done\n");

      } else {
        debug_fprintf(stdout, "SGX session cache HIT\n");
      }
    } else {
        debug_fprintf(stdout, "SSL session cache HIT\n");
    }

    // update current mapping
    sgx_sess = sgx_sp;

    debug_fprintf(stdout, "SGX session mapping key: ");
    print_hex(sgx_sess->id, SGX_SESSION_ID_LENGTH);

    debug_printf("Executing command: %d\n", cmd_pkt.cmd);
    _commands[cmd_pkt.cmd].callback(cmd_pkt, data);
  } 
}

// reads in an operation (in form cmd_len, cmd, data_len, data) from named pipe
// and executes the corresponding command
void
run_command_loop()
{
  cmd_pkt_t cmd_pkt;
  unsigned char data[CMD_MAX_BUF_SIZE];

  // read in operation
  if (sgxbridge_fetch_operation(&cmd_pkt)) {
    if(sgxbridge_fetch_data(data, cmd_pkt.data_len)){
      check_commands(cmd_pkt, data);
    } else {
      debug_fprintf(stderr, "SGX error while fetching data. Exiting...\n");
      sgx_exit(NULL);
    }
  } else {
    // we shouldnt really end up here in normal conditions
    // sgxbridge_fetch_operation does a blocking read on named pipes
    debug_fprintf(stderr, "SGX error while fetching operation.\n");
    sgx_exit(NULL);
  }
}

/* ========================= Command callbacks ============================= */

void
cmd_clnt_rand(cmd_pkt_t cmd_pkt, unsigned char* data)
{
  // TODO: check on data_len?
  memcpy(sgx_sess->s->s3->client_random, data, SSL3_RANDOM_SIZE);

  // DEBUG
  debug_printf("client random:\n");
  print_hex(sgx_sess->s->s3->client_random, cmd_pkt.data_len);
}

void
cmd_srv_rand(cmd_pkt_t cmd_pkt, unsigned char* data)
{
  int random_len = *((int *)data);

  // TODO: check on data len
  arc4random_buf(sgx_sess->s->s3->server_random, SSL3_RANDOM_SIZE);

  // DEBUG
  debug_printf("server random:\n");
  print_hex(sgx_sess->s->s3->server_random, random_len);

  // Send the result
  sgxbridge_pipe_write(sgx_sess->s->s3->server_random, random_len);
}

void
cmd_premaster(cmd_pkt_t cmd_pkt, unsigned char* data)
{
  // decrypt premaster secret (TODO: need to do anyt with i?)
  sgx_sess->premaster_secret_length =
    RSA_private_decrypt(cmd_pkt.data_len,
        data, sgx_sess->premaster_secret, rsa, RSA_PKCS1_PADDING);

  // DEBUG
  debug_printf("decrypted premaster secret:\n");
  print_hex(sgx_sess->premaster_secret,
      sgx_sess->premaster_secret_length);
}

void
cmd_master_sec(cmd_pkt_t cmd_pkt, unsigned char* data)
{
  int ret;
  long *algo2 = (long *) data;
  unsigned char buf[SSL_MAX_MASTER_KEY_LENGTH];

  ret = tls1_PRF(*algo2,
      TLS_MD_MASTER_SECRET_CONST, TLS_MD_MASTER_SECRET_CONST_SIZE,
      sgx_sess->s->s3->client_random, SSL3_RANDOM_SIZE, NULL, 0,
      sgx_sess->s->s3->server_random, SSL3_RANDOM_SIZE, NULL, 0,
      sgx_sess->premaster_secret, sgx_sess->premaster_secret_length,
      sgx_sess->s->session->master_key, buf, sizeof(buf));

  debug_fprintf(stdout, "master key:\n");
  print_hex(sgx_sess->s->session->master_key, SSL_MAX_MASTER_KEY_LENGTH);

#ifndef OPENSSL_WITH_SGX_KEYBLOCK
  sgxbridge_pipe_write(sgx_sess->s->session->master_key,
      SSL_MAX_MASTER_KEY_LENGTH);
#endif
}

void
cmd_rsa_sign(cmd_pkt_t cmd_pkt, unsigned char* data)
{
  unsigned char md_buf[512], signature[512], *q;
  unsigned int sig_size = 0;
  EVP_MD_CTX md_ctx;
 
  int i = 0,
      j = 0,
      num = 0;

  EVP_MD_CTX_init(&md_ctx);
  q = md_buf;
  for (num = 2; num > 0; num--) {
	  if (!EVP_DigestInit_ex(&md_ctx,
			(num == 2) ? EVP_get_digestbyname("MD5") : EVP_get_digestbyname("SHA1"), NULL)) {
		      		puts("Error EVP_DigestInit_ex failed() \n");
		  	  	return;
		}

	  EVP_DigestUpdate(&md_ctx, sgx_sess->s->s3->client_random,
	  SSL3_RANDOM_SIZE);
   	  EVP_DigestUpdate(&md_ctx, sgx_sess->s->s3->server_random,
	  SSL3_RANDOM_SIZE);
	  EVP_DigestUpdate(&md_ctx, data, cmd_pkt.data_len);
	  EVP_DigestFinal_ex(&md_ctx, q, (unsigned int *) &i);
	  q += i;
	  j += i;
	}

  debug_printf("\n Message Digest : len(%d) ", j);

  if (RSA_sign(NID_md5_sha1, md_buf, j, signature, &sig_size,
               private_key->pkey.rsa) <= 0) {
    puts("Error Signing message Digest \n");
  }

  debug_printf("\n Signature : len(%d) ", sig_size);

  sgxbridge_pipe_write((unsigned char *) &sig_size, sizeof(int));
  sgxbridge_pipe_write((unsigned char *) signature, sig_size);
}

void
cmd_rsa_sign_sig_alg(cmd_pkt_t cmd_pkt, unsigned char* data)
{
  unsigned char* md_buf = data;
  char signature[512];
  int sig_size = 0;
  EVP_MD_CTX md_ctx;
  EVP_MD* md = NULL;

  md = SSL_CTX_get_md(ctx);
  if (md == NULL)
    debug_fprintf(stderr, "\n Retriving Digest from ctx failed \n");

  debug_fprintf(stdout, "\n Message Digest : len(%d) \n ", cmd_pkt.data_len);

#if 0
    fflush(stdout);
    print_hex(md_buf, cmd_pkt.data_len);
#endif

  if (!tls12_get_sigandhash((unsigned char *) signature, private_key, md)) {
    puts("Error getting sigandhash ");
  }

  EVP_MD_CTX_init(&md_ctx);
  EVP_SignInit_ex(&md_ctx, md, NULL);
  EVP_SignUpdate(&md_ctx, sgx_sess->s->s3->client_random, SSL3_RANDOM_SIZE);
  EVP_SignUpdate(&md_ctx, sgx_sess->s->s3->server_random, SSL3_RANDOM_SIZE);
  EVP_SignUpdate(&md_ctx, md_buf, cmd_pkt.data_len);

  if (!EVP_SignFinal(&md_ctx,
        (unsigned char *) &signature[4],
        (unsigned int*)&sig_size,
        private_key))
    puts(" Failed to generate the Signature");

  debug_fprintf(stdout, "\n Signature generated successfully : len(%d)\n", sig_size);

#if 0
    fflush(stdout);
    print_hex(&signature[4], sig_size);
    fflush(stdout);
#endif

  sig_size += 4; // Increment for the additional data we computed.

  sgxbridge_pipe_write((unsigned char *) &sig_size, sizeof(int));
  sgxbridge_pipe_write((unsigned char *) signature, sig_size);
}

void
cmd_key_block(cmd_pkt_t cmd_pkt, unsigned char* data){

  int ret;
  sgxbridge_st *sgxb;
  unsigned char *km, *tmp;

  sgxb = (sgxbridge_st *) data;
  km = malloc(sgxb->key_block_len);
  tmp = malloc(sgxb->key_block_len);

  ret = tls1_PRF(sgxb->algo2,
      TLS_MD_KEY_EXPANSION_CONST, TLS_MD_KEY_EXPANSION_CONST_SIZE,
      sgx_sess->s->s3->server_random, SSL3_RANDOM_SIZE,
      sgx_sess->s->s3->client_random, SSL3_RANDOM_SIZE,
      NULL, 0, NULL, 0,
      sgx_sess->s->session->master_key, SSL3_MASTER_SECRET_SIZE,
      km, tmp, sgxb->key_block_len);

  debug_fprintf(stdout, "keyblock (%d):", sgxb->key_block_len);
  print_hex(km, sgxb->key_block_len);

  // FIXME: size has to be mac_secret_size + key_len + iv_len
  if ((sgx_sess->s->s3->tmp.key_block =
        calloc(sgxb->key_block_len, 2)) == NULL) {
          SSLerr(SSL_F_TLS1_SETUP_KEY_BLOCK, ERR_R_MALLOC_FAILURE);
          sgx_exit(NULL);
  }
  debug_fprintf(stdout, "Storing keyblock in temporary struct...");
  memcpy(sgx_sess->s->s3->tmp.key_block, km, sgxb->key_block_len);
  sgx_sess->s->s3->tmp.key_block_length = sgxb->key_block_len;

  // ugly hack for now to only return the nonce/eiv FIXME
  memset(km, 0xFF, 64);
  sgxbridge_pipe_write(km, sgxb->key_block_len);

  debug_fprintf(stdout, "Done\n");

  free(km);
  free(tmp);
}

void
cmd_final_finish_mac(cmd_pkt_t cmd_pkt, unsigned char* data){

  int ret;
  sgxbridge_st *sgxb;
  unsigned char buf2[12];
  unsigned char peer_finish_md[2 * EVP_MAX_MD_SIZE];

  sgxb = (sgxbridge_st *) data;

  ret = tls1_PRF(sgxb->algo2,
      sgxb->str, sgxb->str_len,
      sgxb->buf, sgxb->key_block_len,
      NULL, 0, NULL, 0, NULL, 0,
      sgx_sess->s->session->master_key, SSL3_MASTER_SECRET_SIZE,
      peer_finish_md, buf2, sizeof(buf2));

  int i;
  debug_fprintf(stdout, "final finish MAC:\n");
  for(i = 0; i < 2 * EVP_MAX_MD_SIZE; i++)
      debug_fprintf(stdout, "%x", peer_finish_md[i]);
  debug_fprintf(stdout, "\n");

  // if something went wrong, return length of 1 to indicate an error
  sgxbridge_pipe_write(peer_finish_md, ret ? 2 * EVP_MAX_MD_SIZE : 1);
}

void
cmd_ecdhe_get_public_param(cmd_pkt_t cmd_pkt, unsigned char* data)
{
  const EC_GROUP *group;
  BN_CTX *bn_ctx = NULL;
  int ecdhe_params_size = 0;
  ecdhe_params *ep = (ecdhe_params *) calloc(sizeof(ecdhe_params), 1);

  int *d = (int *) data;
  sgx_sess->ecdh = EC_KEY_new_by_curve_name(*d);
  if (sgx_sess->ecdh == NULL) {
    debug_fprintf(stderr, " EC_KEY_new_by_curve_name() failed \n");
    return;
  }

  if ((EC_KEY_get0_public_key(sgx_sess->ecdh) == NULL)
      || (EC_KEY_get0_private_key(sgx_sess->ecdh) == NULL)) {
    /*(s->options & SSL_OP_SINGLE_ECDH_USE)) { */
    if (!EC_KEY_generate_key(sgx_sess->ecdh)) {
      debug_fprintf(stderr, "EC_KEY_generate_key () failed \n");
      return;
    }
  }

  if ((((group = EC_KEY_get0_group(sgx_sess->ecdh)) == NULL)
        || (EC_KEY_get0_public_key(sgx_sess->ecdh) == NULL)
        || (EC_KEY_get0_private_key(sgx_sess->ecdh)) == NULL)) {
    debug_fprintf(stderr, "EC_KEY_get0_group() failed \n");
    return;
  }

  // For now, we only support ephemeral ECDH  keys over named (not generic)
  // curves. For supported named curves, curve_id is non-zero.
  if ((ep->curve_id = tls1_ec_nid2curve_id(EC_GROUP_get_curve_name(group)))
      == 0) {
    debug_fprintf(stderr, "Failed to retrieve the group curve ID : \n");
    return;
  }

  // Encode the public key. First check the size of encoding and  allocate
  // memory accordingly.
  ep->encoded_length = EC_POINT_point2oct(group,
      EC_KEY_get0_public_key(sgx_sess->ecdh),
      POINT_CONVERSION_UNCOMPRESSED, NULL, 0, NULL);
  if (ep->encoded_length > ENCODED_POINT_LEN_MAX) {
    debug_fprintf(stderr, " No enough memory to hold  ENCODED_POINT!!! %d \n",
        ep->encoded_length);
    return;
  }

  bn_ctx = BN_CTX_new();
  if ((bn_ctx == NULL)) {
    debug_fprintf(stderr, " BN_CTX_new Failed  \n");
    return;
  }

  ep->encoded_length = EC_POINT_point2oct(group,
      EC_KEY_get0_public_key(sgx_sess->ecdh),
      POINT_CONVERSION_UNCOMPRESSED,
      (unsigned char *) ep->encodedPoint,
      ep->encoded_length,
      bn_ctx);

  if (ep->encoded_length == 0) {
    debug_fprintf(stderr, " EC_POINT_point2oct() Failed  \n");
    return;
  }

  debug_fprintf(stderr, "Server EC public key created successfully size(%d) \n",
      ep->encoded_length);
  ep->rsa_public_key_size = EVP_PKEY_size(private_key);

  BN_CTX_free(bn_ctx);
  bn_ctx = NULL;

  ecdhe_params_size = sizeof(ecdhe_params);

  debug_fprintf(stderr, "Private Key %d Data Size %d \n", ep->rsa_public_key_size,
      ecdhe_params_size);

  sgxbridge_pipe_write((unsigned char *) &ecdhe_params_size, sizeof(int));
  sgxbridge_pipe_write((unsigned char *) ep, ecdhe_params_size);
  free(ep);
}

void
cmd_ecdhe_generate_pre_master(cmd_pkt_t cmd_pkt, unsigned char* data)
{
  EC_POINT *clnt_ecpoint = NULL;
  BN_CTX *bn_ctx = NULL;
  const EC_GROUP *group;
  int ec_key_size;

  group = EC_KEY_get0_group(sgx_sess->ecdh);
  if (group == NULL) {
    debug_fprintf(stderr, "EC_KEY_get0_group() failed \n");
    return;
  }

  // Let's get client's public key
  if ((clnt_ecpoint = EC_POINT_new(group)) == NULL) {
    debug_fprintf(stderr, "EC_POINT_new() failed \n");
    return;
  }

  // Get client's public key from encoded point in the ClientKeyExchange
  // message.
  if ((bn_ctx = BN_CTX_new()) == NULL) {
    debug_fprintf(stderr, "BN_CTX_new() failed \n");
    return;
  }

  if (EC_POINT_oct2point(group, clnt_ecpoint, data, cmd_pkt.data_len, bn_ctx) == 0) {
    debug_fprintf(stderr, "EC_POINT_oct2point() failed \n");
    return;
  }

  ec_key_size = ECDH_size(sgx_sess->ecdh);
  if (ec_key_size <= 0) {
    debug_fprintf(stderr, "ECDH_size() failed \n");
    return;
  }

  sgx_sess->premaster_secret_length =
    ECDH_compute_key(data, ec_key_size, clnt_ecpoint, sgx_sess->ecdh, NULL);

  if (sgx_sess->premaster_secret_length <= 0) {
    debug_fprintf(stderr, "ECDH_compute_key() failed \n");
    return;
  }
  debug_fprintf(stderr, " EC_DHE Pre-Master Key computed successfully size(%d) \n",
      sgx_sess->premaster_secret_length);

  memcpy(sgx_sess->premaster_secret,
      data, sgx_sess->premaster_secret_length);

  EC_POINT_free(clnt_ecpoint);
  BN_CTX_free(bn_ctx);
  EC_KEY_free(sgx_sess->ecdh);
}

void
cmd_ssl_handshake_done(cmd_pkt_t cmd_pkt, unsigned char *data)
{
  unsigned char zeros[SSL3_SSL_SESSION_ID_LENGTH];
  memset(zeros, 0, SSL3_SSL_SESSION_ID_LENGTH);

  debug_fprintf(stdout, "Changing mapping key to SSL session ID...");
  lh_SGX_SESSION_delete(sgx_sess_lh, sgx_sess);

  if(memcmp(cmd_pkt.ssl_session_id, zeros, SSL3_SSL_SESSION_ID_LENGTH) == 0){
    // TLS SessionTicket not supported yet
    debug_fprintf(stderr, "TLS Session Ticket not supported\n");
  } else {
    memcpy(sgx_sess->id, cmd_pkt.ssl_session_id, SSL3_SSL_SESSION_ID_LENGTH);
    sgx_sess->type = SSL_SESSION_TYPE;

    lh_SGX_SESSION_insert(ssl_sess_lh, sgx_sess);
  }

  debug_fprintf(stdout, "Done\n");
}

void
cmd_ssl_session_remove(cmd_pkt_t cmd_pkt, unsigned char *data)
{
  debug_fprintf(stdout, "Removing SSL session from cache...");
  lh_SGX_SESSION_delete(ssl_sess_lh, sgx_sess);

  SSL_free(sgx_sess->s);
  debug_fprintf(stdout, "Done\n");
}

void
cmd_change_cipher_state(cmd_pkt_t cmd_pkt, unsigned char *data)
{
  int mac_type = NID_undef, mac_secret_size = 0, status;

  sgx_change_cipher_st *sgx_change_cipher;
  sgx_change_cipher = (sgx_change_cipher_st *) data;

  debug_fprintf(stdout, "Changing cipher state (%ld)...", sgx_change_cipher->cipher_id);
  sgx_sess->s->version = sgx_change_cipher->version;
  sgx_sess->s->mac_flags = sgx_change_cipher->mac_flags;
  sgx_sess->s->method->ssl3_enc->enc_flags = sgx_change_cipher->enc_flags;
  sgx_sess->s->s3->tmp.new_cipher =
    ssl3_get_cipher_by_id(sgx_change_cipher->cipher_id);
  sgx_sess->s->session->cipher = sgx_sess->s->s3->tmp.new_cipher;

  if (sgx_sess->s->session->cipher &&
      (sgx_sess->s->session->cipher->algorithm2 & SSL_CIPHER_ALGORITHM2_AEAD)) {
    if (!ssl_cipher_get_evp_aead(sgx_sess->s->session,
          &sgx_sess->s->s3->tmp.new_aead)) {
          SSLerr(SSL_F_TLS1_SETUP_KEY_BLOCK,
              SSL_R_CIPHER_OR_HASH_UNAVAILABLE);
          sgx_exit(NULL);
    }
  } else {
    if (!ssl_cipher_get_evp(sgx_sess->s->session,
          &sgx_sess->s->s3->tmp.new_sym_enc,
          &sgx_sess->s->s3->tmp.new_hash,
          &mac_type, &mac_secret_size)) {
            SSLerr(SSL_F_TLS1_SETUP_KEY_BLOCK,
                SSL_R_CIPHER_OR_HASH_UNAVAILABLE);
            sgx_exit(NULL);
    }
  }

  sgx_sess->s->s3->tmp.new_mac_pkey_type = mac_type;
  sgx_sess->s->s3->tmp.new_mac_secret_size = mac_secret_size;

  status = tls1_change_cipher_state(
      sgx_sess->s, sgx_change_cipher->which);
  sgxbridge_pipe_write((unsigned char *) &status, sizeof(status));

  debug_fprintf(stdout, "Done\n");
}

void
cmd_sgx_tls1_enc(cmd_pkt_t cmd_pkt, unsigned char *data)
{
  const SSL_AEAD_CTX *aead;
  unsigned char *out, *buf;
  size_t out_len, buf_sz;
  int status = 0;

  sgx_tls1_enc_st *sgx_tls1_enc;
  sgx_tls1_enc = (sgx_tls1_enc_st *) data;

  out = data + sizeof(sgx_tls1_enc_st);
  if(sgx_tls1_enc->send){
    debug_fprintf(stdout, "Sealing input buffer (%zu)...",
        sgx_tls1_enc->len + sgx_tls1_enc->eivlen);

    aead = sgx_sess->s->aead_write_ctx;

    if (!(status = EVP_AEAD_CTX_seal(&aead->ctx,
        out + sgx_tls1_enc->eivlen, &out_len,
        sgx_tls1_enc->len + aead->tag_len, sgx_tls1_enc->nonce,
        sgx_tls1_enc->nonce_used,
        data + sizeof(sgx_tls1_enc_st) + sgx_tls1_enc->eivlen,
        sgx_tls1_enc->len, sgx_tls1_enc->ad, sizeof(sgx_tls1_enc->ad))))

        debug_fprintf(stderr, "SGX seal() failed: %d\n", status);
  } else {
    debug_fprintf(stdout, "Opening input buffer (%zu)...\n",
        sgx_tls1_enc->len + sgx_tls1_enc->eivlen);
    print_hex(data + sizeof(sgx_tls1_enc_st), sgx_tls1_enc->len);

    aead = sgx_sess->s->aead_read_ctx;

    if (!(status = EVP_AEAD_CTX_open(&aead->ctx,
            out, &out_len, sgx_tls1_enc->len, sgx_tls1_enc->nonce,
            sgx_tls1_enc->nonce_used, data + sizeof(sgx_tls1_enc_st),
            sgx_tls1_enc->len + aead->tag_len, sgx_tls1_enc->ad,
            sizeof(sgx_tls1_enc->ad))))

        debug_fprintf(stderr, "SGX open() failed: %d\n", status);
  }

  buf_sz = sizeof(size_t) + sizeof(int) + out_len + sgx_tls1_enc->eivlen;
  buf = malloc(buf_sz);

  memcpy(buf, &out_len, sizeof(size_t));
  memcpy(buf + sizeof(size_t), &status, sizeof(int));
  memcpy(buf + sizeof(size_t) + sizeof(int), out,
      out_len + sgx_tls1_enc->eivlen);

  sgxbridge_pipe_write(buf, buf_sz);

  free(buf);
  debug_fprintf(stdout, "Done\n");
}
