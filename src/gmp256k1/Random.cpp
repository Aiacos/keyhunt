#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gmp.h>


#if  defined(_WIN32) || defined(_WIN64)
    #include <Windows.h>
    #include <bcrypt.h>
    #pragma comment(lib, "bcrypt.lib")
#elif __unix__ || __unix || __APPLE__ || __MACH__ || __CYGWIN__
    #include <unistd.h>
    #include <fcntl.h>
    #include <sys/syscall.h>
    #include <linux/random.h>
    #if defined(GRND_NONBLOCK)
        #define USE_GETRANDOM
    #endif
#endif

#include "Int.h"

static int r_state_mt_ready = 0;
static gmp_randstate_t r_state_mt;


/**
 * @brief Initialize the Mersenne Twister random state.
 * @return 0 on success, -1 if already initialized or RNG unavailable.
 */
int int_randominit()	{
	if(r_state_mt_ready)	{
		/* Already initialized - return error instead of exit() */
		fprintf(stderr,"r_state_mt already initialized, file %s, line %i\n",__FILE__,__LINE__ - 1);
		return -1;
	}
	mpz_t mpz_seed;
	int bytes_readed,bytes = 64;
	unsigned char seed[64];
	bytes_readed = random_bytes(seed, bytes);
	if(bytes_readed != bytes)	{
		/* RNG failure - return error instead of exit() */
		fprintf(stderr,"Error random_bytes(), file %s, line %i\n",__FILE__,__LINE__ - 2);
		return -1;
	}
	mpz_init(mpz_seed);
	mpz_import(mpz_seed,bytes,1,sizeof(unsigned char),0,0,seed);
	gmp_randinit_mt(r_state_mt);
	gmp_randseed(r_state_mt,mpz_seed);
	r_state_mt_ready = 1;
	mpz_clear(mpz_seed);
	memset(seed,0,bytes);
	return 0;
}

void Int::Rand(int nbit)	{
	if(!r_state_mt_ready)	{
		/* Auto-initialize if not ready - error message only if init fails */
		if (int_randominit() != 0) {
			fprintf(stderr,"Error Rand(): RNG initialization failed, file %s, line %i\n",__FILE__,__LINE__ - 1);
			/* Set to zero as fallback instead of exit() */
			mpz_set_ui(num, 0);
			return;
		}
	}
	mpz_urandomb(num,r_state_mt,nbit);
	mpz_setbit(num,nbit-1);
}

void Int::Rand(Int *min,Int *max)	{
	if(!r_state_mt_ready)	{
		/* Auto-initialize if not ready - error message only if init fails */
		if (int_randominit() != 0) {
			fprintf(stderr,"Error Rand(): RNG initialization failed, file %s, line %i\n",__FILE__,__LINE__ - 1);
			/* Set to min as fallback instead of exit() */
			this->Set(min);
			return;
		}
	}
	Int diff(max);
	diff.Sub(min);
	this->Rand(256);
	this->Mod(&diff);
	this->Add(min);
}

int random_bytes(unsigned char *buffer,int bytes)	{
    #if defined(_WIN32) || defined(_WIN64)
        if (!BCryptGenRandom(NULL, buffer, bytes, BCRYPT_USE_SYSTEM_PREFERRED_RNG)) {
            fprintf(stderr,"BCryptGenRandom failed\n");
            /* Return -1 instead of exit() - let caller handle the error */
            return -1;
        }
        return bytes;
	#elif __unix__ || __unix || __APPLE__ || __MACH__ || __CYGWIN__
		#ifdef USE_GETRANDOM
			int result = syscall(SYS_getrandom, buffer, bytes, GRND_NONBLOCK);
			return (result < 0) ? -1 : result;
		#else
            int fd = open("/dev/urandom", O_RDONLY);
            if (fd == -1) {
				fprintf(stderr,"/dev/urandom unavailable\n");
				/* Return -1 instead of exit() - let caller handle the error */
				return -1;
            }
            ssize_t result = read(fd, buffer, bytes);
            close(fd);
			return (result < 0) ? -1 : (int)result;
        #endif
    #else
        #error "Unsupported platform"
    #endif
}
