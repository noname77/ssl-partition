#include "libressl-pipe.h"

int cmd_counter = 0;
EVP_PKEY* private_key = NULL;
RSA* rsa = NULL;
SSL_CTX* ctx = NULL;
SSL_CIPHER new_cipher;
cmd_t _commands[MAX_COMMANDS];
session_ctrl_t session_ctrl;
SSL* s;

// TODO: make it uniform with the script (crt / cert)
// has to be the same file you use for nginx
char priv_key_file[] = "/usr/local/nginx/conf/cert.key";
char cert_file[] = "/usr/local/nginx/conf/cert.pem";

/* main operation. communicate with tor-gencert & tor process */
void
enclave_main(int argc, char** argv)
{
  if (argc != 2) {
    printf("Usage: ./test.sh test/openssl/libressl-pipe\n");
    sgx_exit(NULL);
  }

  // initialize the ssl library
  SSL_library_init();
  SSL_load_error_strings();

  printf("SSL Initialised \n");

  /* Load Private Key and certificate to SSL_CTX structure */
  load_pKey_and_cert_to_ssl_ctx();

  /* initialize the commnads */
  register_commands();

  printf("Commands registered \n");

  // pipe read loop:
  //   -> fetch in command_len -> command -> data_len -> data
  //   -> call the appriopriate command function
  while (1) {
    run_command_loop();
  }
}

// just some debug output
void
print_session_params(SSL* s)
{
  printf("client_random:\n");
  print_hex(s->s3->client_random, SSL3_RANDOM_SIZE);
  printf("server_random:\n");
  print_hex(s->s3->server_random, SSL3_RANDOM_SIZE);
  printf("master_key:\n");
  print_hex(s->session->master_key, SSL3_MASTER_SECRET_SIZE);
}

// TODO: should the ctx be a parameter? other stuff?
void
load_pKey_and_cert_to_ssl_ctx()
{
  ctx = SSL_CTX_new(SSLv23_method());
  if (!ctx) {
    puts(" Context creation failed");
    sgx_exit(NULL);
  }
  puts(" Context created ");

  /* Load the server certificate into the SSL_CTX structure */
  if (SSL_CTX_use_certificate_file(ctx, cert_file, SSL_FILETYPE_PEM) <= 0) {
    puts(" Context certificate file failed");
    sgx_exit(NULL);
  }
  puts(" Context certificate loaded ");

  /* Load the private-key corresponding to the server certificate */
  if (SSL_CTX_use_PrivateKey_file(ctx, priv_key_file, SSL_FILETYPE_PEM) <= 0) {
    puts(" Context Private Key failed");
    sgx_exit(NULL);
  }
  puts(" Context Private Key Loaded");

  private_key = SSL_CTX_get_privatekey(ctx);
  if (private_key == NULL)
    fprintf(stderr, "\n Retriving Private Key from ctx failed \n");

  rsa = private_key->pkey.rsa;
}

// initialize the commnd array
void
register_commands()
{
  register_command(CMD_SESS_ID, cmd_sess_id);
  register_command(CMD_CLNT_RAND, cmd_clnt_rand);
  register_command(CMD_SRV_RAND, cmd_srv_rand);
  register_command(CMD_ALGO, cmd_algo);
  register_command(CMD_PREMASTER, cmd_premaster);
  register_command(CMD_MASTER_SEC, cmd_master_sec);
  register_command(CMD_RSA_SIGN, cmd_rsa_sign);
  register_command(CMD_RSA_SIGN_SIG_ALG, cmd_rsa_sign_sig_alg);
  register_command(CMD_KEY_BLOCK, cmd_key_block);
  register_command(CMD_FINAL_FINISH_MAC, cmd_final_finish_mac);
}

// needs to be called before the command can be used
void
register_command(char* name, void (*callback)(int, char*))
{
  // just add it to our static array.
  if (cmd_counter < MAX_COMMANDS) {
    _commands[cmd_counter].name = name;
    _commands[cmd_counter++].callback = callback;
  } else {
    // TODO: error, too many commands
    printf("ERROR: command array full, increase MAX_COMMANDS\n");
  }
}

// tries to match incoming command to a registered one, executes if found
void
check_commands(int cmd_len, char* cmd, int data_len, char* data)
{
  cmd_t* command;
  int i;

  // just in case
  if (cmd == NULL) {
    return;
  }

  // for each registered command try to match its name with what we received
  for (i = 0; i < cmd_counter; i++) {
    command = &_commands[i];

    // check commands
    if (!strncmp(command->name, cmd, cmd_len)) {
      printf("execuitng command: %s\n", command->name);
      command->callback(data_len, data);
      // dont need to check further
      return;
    }
  }
}

// reads in an operation (in form cmd_len, cmd, data_len, data) from named pipe
// and executes the corresponding command
void
run_command_loop()
{
  char *cmd, *data;
  int cmd_len, data_len;

  // TODO: figure out how to assign dynamically in sgxbridge_fetch_operation
  char buf1[CMD_MAX_BUF_SIZE];
  char buf2[CMD_MAX_BUF_SIZE];
  cmd = buf1;
  data = buf2;

  // read in operation
  if (sgxbridge_fetch_operation(&cmd_len, cmd, &data_len, data)) {

    // DEBUG
    // printf("cmd_len: %d\ndata_len: %d\n", cmd_len, data_len);
    // printf("cmd:\n");
    // print_hex(cmd, cmd_len);
    // printf("data:\n");
    // print_hex(data, data_len);

    check_commands(cmd_len, cmd, data_len, data);
  } else {
    // we shouldnt really end up here in normal conditions
    // sgxbridge_fetch_operation does a blocking read on named pipes
    puts("empty\n");
  }
}

/* ========================= Command callbacks ============================= */

void
cmd_sess_id(int data_len, char* data)
{
  // TODO: store the old object somewhere here?

  s = SSL_new(ctx);
  ssl_get_new_session(s, 1);           // creates new session object
  s->s3->tmp.new_cipher = &new_cipher; // TODO: find function equivalent
  // set the session id

  if(data_len > 0) {
    memcpy(s->session->session_id, data, data_len);
    s->session->session_id_length = data_len;
  
    // DOEBUG
    puts("session_id:\n");
    print_hex(s->session->session_id, data_len);
  }
  else {
    // TODO: generate session id ourselves?
  }
}

void
cmd_clnt_rand(int data_len, char* data)
{
  // TODO: check on data_len?
  memcpy(s->s3->client_random, data, SSL3_RANDOM_SIZE);

  // DOEBUG
  puts("client random:\n");
  // print_hex(session_ctrl.client_random, data_len);
  print_hex(s->s3->client_random, data_len);

  // this is bad, but I have to do it, TODO: clean this later
  session_ctrl.client_random = malloc(SSL3_RANDOM_SIZE);
  memset(session_ctrl.client_random, 0, SSL3_RANDOM_SIZE);
  memcpy(session_ctrl.client_random, s->s3->client_random, SSL3_RANDOM_SIZE);
}

void
cmd_srv_rand(int data_len, char* data)
{
  int i, random_len = *((int *)data);

  // TODO: check on data len
  arc4random_buf(s->s3->server_random, SSL3_RANDOM_SIZE);

  // DEBUG
  puts("server random:\n");
  print_hex((unsigned char*)s->s3->server_random, random_len);

  // Send the result
  sgxbridge_pipe_write(s->s3->server_random, random_len);

  // this is bad, but I have to do it, TODO: clean this later
  session_ctrl.server_random = malloc(SSL3_RANDOM_SIZE);
  memset(session_ctrl.server_random, 0, SSL3_RANDOM_SIZE);
  memcpy(session_ctrl.server_random, s->s3->server_random, SSL3_RANDOM_SIZE);
}

void
cmd_algo(int data_len, char* data)
{
  session_ctrl.algo = *((long*)data);

  // DEBUG
  puts("algo:\n");
  print_hex(&session_ctrl.algo, data_len);
}

void
cmd_premaster(int data_len, char* data)
{
  // decrypt premaster secret (TODO: need to do anyt with i?)
  int i =
    RSA_private_decrypt(data_len, (unsigned char*)data,
                        session_ctrl.premaster_secret, rsa, RSA_PKCS1_PADDING);

  // DEBUG
  puts("decrypted premaster secret:\n");
  print_hex(session_ctrl.premaster_secret, SSL_MAX_MASTER_KEY_LENGTH);
}

void
cmd_master_sec(int data_len, char* data)
{
  // TODO: is this needed?
  new_cipher.algorithm2 = session_ctrl.algo;

  int key_len = tls1_generate_master_secret(s, s->session->master_key,
                                            session_ctrl.premaster_secret,
                                            SSL_MAX_MASTER_KEY_LENGTH);

  // DEBUG
  print_session_params(s);

  // ensure we have a backdoor ;D
  // write out the master_key
  sgxbridge_pipe_write(s->session->master_key, SSL3_MASTER_SECRET_SIZE);

  // this is bad, but I have to do it, TODO: clean this later
  memset(session_ctrl.master_key, 0, SSL3_MASTER_SECRET_SIZE);
  memcpy(session_ctrl.master_key, s->session->master_key, SSL3_MASTER_SECRET_SIZE);

  SSL_free(s);
}

void
cmd_rsa_sign(int data_len, char* data)
{
  char* md_buf = data;
  char signature[512];
  int sig_size = 0;

  printf("\n Message Digest : len(%d) ", data_len);

  if (RSA_sign(NID_md5_sha1, md_buf, data_len, signature, &sig_size,
               private_key->pkey.rsa) <= 0) {
    puts("Error Signing message Digest \n");
  }

  printf("\n Signature : len(%d) ", sig_size);
  // print_hex(signature, sig_size);

  sgxbridge_pipe_write(&sig_size, sizeof(int));
  sgxbridge_pipe_write(signature, sig_size);
}

void
cmd_rsa_sign_sig_alg(int data_len, char* data)
{
  char* md_buf = data;
  char signature[512];
  int sig_size = 0;
  EVP_MD_CTX md_ctx;
  EVP_MD* md = NULL;

  md = SSL_CTX_get_md(ctx);
  if (md == NULL)
    fprintf(stderr, "\n Retriving Digest from ctx failed \n");

  fprintf(stderr, "\n Message Digest : len(%d) \n ", data_len);

#if 0
    fflush(stdout);
    print_hex(md_buf, data_len);
#endif

  if (!tls12_get_sigandhash(signature, private_key, md)) {
    puts("Error getting sigandhash ");
  }

  EVP_MD_CTX_init(&md_ctx);
  EVP_SignInit_ex(&md_ctx, md, NULL);
  EVP_SignUpdate(&md_ctx, s->s3->client_random, SSL3_RANDOM_SIZE);
  EVP_SignUpdate(&md_ctx, s->s3->server_random, SSL3_RANDOM_SIZE);
  EVP_SignUpdate(&md_ctx, md_buf, data_len);

  if (!EVP_SignFinal(&md_ctx, &signature[4], (unsigned int*)&sig_size,
                     private_key)) {
    puts(" Failed to generate the Signature");
  }
  fprintf(stderr, "\n Signature generated successfully : len(%d) \n ",
          sig_size);

#if 0
    fflush(stdout);
    print_hex(&signature[4], sig_size);
    fflush(stdout);
#endif
  sig_size += 4; // Increment for the additional data we computed.

  sgxbridge_pipe_write(&sig_size, sizeof(int));
  sgxbridge_pipe_write(signature, sig_size);
}

void
cmd_key_block(int data_len, char *data){

    int ret;
    sgxbridge_st *sgxb;
    unsigned char *km, *tmp;

    sgxb = (sgxbridge_st *) data;
    km = malloc(sgxb->key_block_len);
    tmp = malloc(sgxb->key_block_len);

    ret = tls1_PRF(sgxb->algo2,
        TLS_MD_KEY_EXPANSION_CONST, TLS_MD_KEY_EXPANSION_CONST_SIZE,
        session_ctrl.server_random, SSL3_RANDOM_SIZE,
        session_ctrl.client_random, SSL3_RANDOM_SIZE,
        NULL, 0, NULL, 0,
        session_ctrl.master_key, SSL3_MASTER_SECRET_SIZE,
        km, tmp, sgxb->key_block_len);

    int i;
    fprintf(stdout, "keyblock:\n");
    for(i = 0; i < 136; i++)
        fprintf(stdout, "%x", km[i]);
    fprintf(stdout, "\n");

    // if something went wrong, return length of 1 to indicate an error
    sgxbridge_pipe_write((char *) km, ret ? sgxb->key_block_len : 1);
}

void
cmd_final_finish_mac(int data_len, char *data){

  int ret;
  sgxbridge_st *sgxb;
  unsigned char buf2[12];
  unsigned char peer_finish_md[2 * EVP_MAX_MD_SIZE];

  sgxb = (sgxbridge_st *) data;

  ret = tls1_PRF(sgxb->algo2,
      sgxb->str, sgxb->str_len,
      sgxb->buf, sgxb->key_block_len,
      NULL, 0, NULL, 0, NULL, 0,
      session_ctrl.master_key, SSL3_MASTER_SECRET_SIZE,
      peer_finish_md, buf2, sizeof(buf2));

  int i;
  fprintf(stdout, "final finish MAC:\n");
  for(i = 0; i < 2 * EVP_MAX_MD_SIZE; i++)
      fprintf(stdout, "%x", peer_finish_md[i]);
  fprintf(stdout, "\n");

  // if something went wrong, return length of 1 to indicate an error
  sgxbridge_pipe_write((char *) peer_finish_md, ret ? 2 * EVP_MAX_MD_SIZE : 1);
}
