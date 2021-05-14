#include "finders.h"
#include <stdio.h>
#include <stdlib.h>

#include <unistd.h>
#include <getopt.h>
#include <pthread.h>
const uint64_t STRUCT_MASK = 0xffff;  // when to check on neighbouring threads
const uint32_t BIOME_MASK = 0x3f;
const int MINECRAFT_VERSION = MC_1_16;

#define MAXBUFLEN 60000
#define VERSION 0xff //id for the validation
#define DEBUG 0
#include <string.h>
#include <time.h>
#include "./minecraft_nether_gen_rs.h"

void int64ToChar(unsigned char a[], int64_t n) {
  a[0] = (n >> 56) & 0xFF;
  a[1] = (n >> 48) & 0xFF;
  a[2] = (n >> 40) & 0xFF;
  a[3] = (n >> 32) & 0xFF;
  a[4] = (n >> 24) & 0xFF;
  a[5] = (n >> 16) & 0xFF;
  a[6] = (n >> 8) & 0xFF;
  a[7] = n & 0xFF;
}

uint64_t charTo64bitNum(char a[]) {
  uint64_t n = (unsigned long) (a[7] & 0xFF);
  n |= (unsigned long) (a[6] & 0xFF) << 8;
  n |= (unsigned long) (a[5] & 0xFF) << 16;
  n |= (unsigned long) (a[4] & 0xFF) << 24;
  n |= (unsigned long) (a[3] & 0xFF) << 32;
  n |= (unsigned long) (a[2] & 0xFF) << 40;
  n |= (unsigned long) (a[1] & 0xFF) << 48;
  n |= (unsigned long) (a[0] & 0xFF) << 56;
  return n;
}

void print64(int64_t n){
  unsigned char chars[8];
  int64ToChar(chars, n);
  int i;
  for (i = 0; i < 8; i++){
    printf("%02x", chars[i]); /* print the result */
  }
  return;
}

void print32(unsigned int n){
  unsigned char chars[4];
  chars[0] = (n >> 24) & 0xFF;
  chars[1] = (n >> 16) & 0xFF;
  chars[2] = (n >> 8) & 0xFF;
  chars[3] = n & 0xFF;
  int i;
  for (i = 0; i < 4; i++){
    printf("%02x", chars[i]); /* print the result */
  }
  return;
}

long l2norm(long x1, long z1, long x2, long z2){
  return (x1-x2)*(x1-x2) + (z1-z2)*(z1-z2);
}

uint64_t rand64()
{
  uint64_t rv = 0;
  int c,i;
  FILE *fp;
  fp = fopen("/dev/urandom", "r");

  for (i=0; i < sizeof(rv); i++) {
     do {
       c = fgetc(fp);
     } while (c < 0);
     rv = (rv << 8) | (c & 0xff);
  }
  fclose(fp);
  return rv;
}

//THE FILTERS: ethos these output a 0 for fail 1 for succeed and check either lower48 bits (fast filter) or top16 (slow filter)

//this is classic FSG fastion without biome (pos/pos bastion, tells you neg/pos or pos/neg fortress)
char netherchecker(int64_t seed, int* fortressQuadrant){
  //return true if the nether is good (3 structures within -128 to 128 ignoring neg/neg) at 
  unsigned long modulus = 1ULL << 48;
  unsigned long AA = 341873128712;
  unsigned long BB = 132897987541;
  int bastionCount = 0;
  *fortressQuadrant = 0;
  int64_t fakeseed = (seed + 30084232ULL) ^ 0x5deece66dUL;
  int64_t chunkx = next(&fakeseed, 31) % 23;
  int64_t chunkz = next(&fakeseed, 31) % 23;
  int structureType = (next(&fakeseed, 31) % 5)  >= 2;
  bastionCount += structureType;
  if (chunkx > 8 || chunkz > 8 || structureType == 0){
    return 0;
  }
  int gotfort = 0;
  fakeseed = (seed + 30084232UL - AA) ^ 0x5deece66dUL;
  chunkx = next(&fakeseed, 31) % 23;
  chunkz = next(&fakeseed, 31) % 23;
  structureType = (next(&fakeseed, 31) % 5)  >= 2;
  bastionCount += structureType;
  if (structureType == 0){
    *fortressQuadrant = -1;
  }
  if (chunkx >= 19 && chunkz <= 8 && structureType == 0){
    return 1;
  }

  fakeseed = (seed + 30084232UL - BB) ^ 0x5deece66dUL;
  chunkx = next(&fakeseed, 31) % 23;
  chunkz = next(&fakeseed, 31) % 23;
  structureType = (next(&fakeseed, 31) % 5)  >= 2;
  bastionCount += structureType;
  if (structureType == 0){
    *fortressQuadrant = 1;
  }
  if (chunkx <= 8 && chunkz >= 19 && structureType == 0){
    return 1;
  }

  return 0;
}

//checks for basalt deltas at bastion location
char bastionbiome(uint64_t seed){
  int64_t fakeseed = (seed + 30084232ULL) ^ 0x5deece66dUL;
  int64_t chunkx = next(&fakeseed, 31) % 23;
  int64_t chunkz = next(&fakeseed, 31) % 23;
  NetherGen* netherGen=create_new_nether(seed);  
  NetherBiomes biome=get_biome(netherGen,chunkx*16,0,chunkz*16);
  if (biome==BasaltDeltas){
    delete(netherGen);
    return 0;
  }
  delete(netherGen);
  return 1;
}

//casts a drag net looking for any ravine start that will be low, wide, and flat
int ravinePositionAndQuality(int64_t seed){
  int64_t fakeseed, carvea, carveb;
  int carver_offset = 0;
  long chx, chz, i;
  long magmax, magmaz, magmay;
  long partialy, maxLength;
  float temp, pitch, width;
  for (chx = -4; chx < 16; chx++){
    for (chz = -4; chz < 16; chz++){
      fakeseed = (seed + carver_offset) ^ 0x5deece66dL;
      carvea = nextLong(&fakeseed);
      carveb = nextLong(&fakeseed);
      fakeseed = ((chx * carvea) ^ (chz * carveb) ^ (seed + carver_offset)) ^ 0x5deece66dL;
      fakeseed = fakeseed & 0xFFFFFFFFFFFF;
      temp = nextFloat(&fakeseed);
      if (temp < .02){
        Pos pos;
        magmax = chx * 16 + nextInt(&fakeseed, 16);
        partialy = nextInt(&fakeseed, 40) + 8;
        magmay = 20 + nextInt(&fakeseed, partialy);
        magmaz = chz * 16 + nextInt(&fakeseed, 16);
        pos.x = magmax;
        pos.z = magmaz;
        nextFloat(&fakeseed);
        pitch = (nextFloat(&fakeseed) - 0.5F) * 2.0F / 8.0F;
        temp = nextFloat(&fakeseed);
        width = (temp*2.0F + nextFloat(&fakeseed))*2.0F;
        maxLength = 112L - nextInt(&fakeseed, 28);
        if ( magmay < 25 && (pitch < 0.11 && pitch > -.11 ) && width > 2.5){
          return 1;
        }
      }
    }
  }
  return 0;
}

//casts a drag net for low, wide, flat, and ocean (not warm)
int ravineBiome(int64_t seed, LayerStack* gp){
  int64_t fakeseed, carvea, carveb;
  int carver_offset = 0;
  long chx, chz, i;
  long magmax, magmaz, magmay;
  long partialy, maxLength;
  float temp, pitch, width;
  for (chx = -4; chx < 16; chx++){
    for (chz = -4; chz < 16; chz++){
      fakeseed = (seed + carver_offset) ^ 0x5deece66dL;
      carvea = nextLong(&fakeseed);
      carveb = nextLong(&fakeseed);
      fakeseed = ((chx * carvea) ^ (chz * carveb) ^ (seed + carver_offset)) ^ 0x5deece66dL;
      fakeseed = fakeseed & 0xFFFFFFFFFFFF;
      temp = nextFloat(&fakeseed);
      if (temp < .02){
        Pos pos;
        magmax = chx * 16 + nextInt(&fakeseed, 16);
        partialy = nextInt(&fakeseed, 40) + 8;
        magmay = 20 + nextInt(&fakeseed, partialy);
        magmaz = chz * 16 + nextInt(&fakeseed, 16);
        pos.x = magmax;
        pos.z = magmaz;
        nextFloat(&fakeseed);
        pitch = (nextFloat(&fakeseed) - 0.5F) * 2.0F / 8.0F;
        temp = nextFloat(&fakeseed);
        width = (temp*2.0F + nextFloat(&fakeseed))*2.0F;
        maxLength = 112L - nextInt(&fakeseed, 28);
        if ( magmay < 25 && (pitch < 0.11 && pitch > -.11 ) && width > 2.5){
          int biomeAtPos = getBiomeAtPos(gp, pos);
          if (isOceanic(biomeAtPos) && (biomeAtPos !=  lukewarm_ocean &&  biomeAtPos != deep_lukewarm_ocean && biomeAtPos != warm_ocean && biomeAtPos != deep_warm_ocean) ){
            return 1;
          }
        }
      }
    }
  }
  return 0;
}

//Puts a village within 0 to 96
int villageLocation(int64_t lower48){
  const StructureConfig sconf = VILLAGE_CONFIG;
  int valid;
  Pos p = getStructurePos(sconf, lower48, 0, 0, &valid);
  if (!valid || (p.x < 96 && p.z < 96) || p.x > 144 || p.z > 144 ){
    return 0;
  }
  return 1;
}

//Puts a jungle temple within 0 to 96
int jungleLocation(int64_t lower48){
  const StructureConfig sconf = JUNGLE_PYRAMID_CONFIG;
  int valid;
  Pos p = getStructurePos(sconf, lower48, 0, 0, &valid);
  if (!valid || p.x > 96 || p.z > 96){
    return 0;
  }
  return 1;
}

int shipwreckLocationAndType(int64_t seed){ //we will presume ocean not beach (test for speed)
  int valid3;
  const StructureConfig sconf_shipwreck = SHIPWRECK_CONFIG;
  Pos p3 = getStructurePos(sconf_shipwreck, seed, 0, 0, &valid3);
  if (!valid3 || p3.x >= 96 || p3.z >= 96){
    return 0;
  }
  unsigned long modulus = 1UL << 48;
  int64_t fakeseed, carvea, carveb;
  int shipchunkx, shipchunkz, portOceanType, portNormalY, portNormalType;
  int shiptype;
  shipchunkx = p3.x >> 4;
  shipchunkz = p3.z >> 4;
  fakeseed = (seed) ^ 0x5deece66dL;
  carvea = nextLong(&fakeseed);
  carveb = nextLong(&fakeseed);
  fakeseed = ((shipchunkx * carvea) ^ (shipchunkz * carveb) ^ seed) ^ 0x5deece66dL;
  fakeseed = fakeseed & 0xFFFFFFFFFFFF;
  fakeseed = (0x5deece66dUL*fakeseed + 11) % modulus ; //advance 2
  fakeseed = (0x5deece66dUL*fakeseed + 11) % modulus ;
  shiptype = (fakeseed >> 17) % 20;
  if (shiptype == 2 || shiptype == 5 || shiptype == 8 || shiptype == 12 || shiptype == 15 || shiptype == 18){
    return 0; //rejecting front only ships allowing all others
  }
  return 1;
}

int portalLocation(int64_t seed, int villageMode, int* px, int* pz){
  const StructureConfig sconf_portal = RUINED_PORTAL_CONFIG;
  int valid2;
  Pos p2 = getStructurePos(sconf_portal, seed, 0, 0, &valid2);
  if (villageMode == 1){
    if (!valid2 || p2.x >= 96 || p2.z >= 96){// || p2.x >= 144 || p2.z >= 144){
      return 0;
    }
    *px = p2.x;
    *pz = p2.z;
    return 1;
  }
  if (!valid2 || p2.x > 144 || p2.z > 144){
    return 0;
  }
  *px = p2.x;
  *pz = p2.z;
  return 1;
}

int portalBiome(int64_t seed, LayerStack* gp){
  const StructureConfig sconf = RUINED_PORTAL_CONFIG;
  int valid;
  Pos p = getStructurePos(sconf, seed, 0, 0, &valid);
  int mc = MC_1_16;
  int biome = getBiomeAtPos(gp, p);
  if (isOceanic(biome)){
    return 0;
  }
  if (biome == desert || biome == snowy_tundra){
    return 0;
  }
  if (!isViableStructurePos(sconf.structType, mc, gp, seed, p.x, p.z)){
    return 0;
  }
  return 1;
}

//there are 3 biome classes to handle when filtering ruined_portal TYPE
int portalTypeJungle(int64_t seed){
  const StructureConfig sconf_portal = RUINED_PORTAL_CONFIG;
  int valid2, portalType;
  Pos p2 = getStructurePos(sconf_portal, seed, 0, 0, &valid2);
  unsigned long modulus = 1UL << 48;
  int portcx, portcz, portOceanType, portNormalY, portNormalType;
  float buriedFloat, bigOrSmall;
  int rawPortalType;
  int64_t fakeseed, carvea, carveb;
  portcx = p2.x >> 4;
  portcz = p2.z >> 4;
  fakeseed = (seed) ^ 0x5deece66dL;
  carvea = nextLong(&fakeseed);
  carveb = nextLong(&fakeseed);
  fakeseed = ((portcx * carvea) ^ (portcz * carveb) ^ seed) ^ 0x5deece66dL;
  fakeseed = fakeseed & 0xFFFFFFFFFFFF;
  next(&fakeseed, 31); //tossed out for jungle terrains (air gaps)
  //  buriedFloat = nextFloat(&fakeseed); // jungle never buries it
  bigOrSmall = nextFloat(&fakeseed); // 1/20 chance of being big
  rawPortalType = next(&fakeseed, 31); //once this is reduced mod 3 or 10 we know the type
  if (bigOrSmall < .05){
    return 1; //all three big ones have enough lava and we're not underground
  }
  portalType = rawPortalType % 10;
  if (portalType == 0 || portalType == 2 || portalType == 3 || portalType == 4 || portalType == 5 || portalType == 8){
    return 0; //6 small types have no lava
  }
  return 1; //this seed made it
}

int portalTypeOcean(int64_t seed){
  return 1; //in this case we're either buried or underwater so the lava doesn't matter
  //this is really just a chest and gold
  //IF the world demands portal Type filtering for the ocean/desert worlds I would do it here
}

int portalTypeNormal(int64_t seed){
  const StructureConfig sconf_portal = RUINED_PORTAL_CONFIG;
  int valid2, portalType;
  Pos p2 = getStructurePos(sconf_portal, seed, 0, 0, &valid2);
  int portcx, portcz, portOceanType, portNormalY, portNormalType;
  float buriedFloat, bigOrSmall;
  int rawPortalType;
  int64_t fakeseed, carvea, carveb;
  portcx = p2.x >> 4;
  portcz = p2.z >> 4;
  fakeseed = (seed) ^ 0x5deece66dL;
  carvea = nextLong(&fakeseed);
  carveb = nextLong(&fakeseed);
  fakeseed = ((portcx * carvea) ^ (portcz * carveb) ^ seed) ^ 0x5deece66dL;
  fakeseed = fakeseed & 0xFFFFFFFFFFFF;
  buriedFloat = nextFloat(&fakeseed); // 50/50 shot at being underground
  next(&fakeseed, 31); //tossed out for normal terrains (air gaps)
  bigOrSmall = nextFloat(&fakeseed); // 1/20 chance of being big
  rawPortalType = next(&fakeseed, 31); //once this is reduced mod 3 or 10 we know the type
  if (buriedFloat < .5){
    return 0;
  }
  if (bigOrSmall < .05){
    return 1; //all three big ones have enough lava and we're not underground
  }
  portalType = rawPortalType % 10;
  if (portalType == 0 || portalType == 2 || portalType == 3 || portalType == 4 || portalType == 5 || portalType == 8){
    return 0; //6 small types have no lava
  }
  return 1; //this seed made it
}

int strongholdAngle(int64_t seed, int fortressQuadrant){
  StrongholdIter sh;
  int mc = MC_1_16;
  Pos pos_sh = initFirstStronghold(&sh, mc, seed);
  long temp1, temp2, temp3;
  if (fortressQuadrant == -1){
    temp1 = l2norm(pos_sh.x, pos_sh.z, -1200L, 1200L);
    temp2 = l2norm(pos_sh.x, pos_sh.z, 1639L, 439L);
    temp3 = l2norm(pos_sh.x, pos_sh.z, -439L, -1639L);
    if ((temp1 > 300*300) && (temp2 > 300*300) && (temp3 > 300*300)){
      return 0;
    }
  }
  if (fortressQuadrant == 1){
    temp1 = l2norm(pos_sh.x, pos_sh.z, 1200L, -1200L);
    temp2 = l2norm(pos_sh.x, pos_sh.z, -1639L, -439L);
    temp3 = l2norm(pos_sh.x, pos_sh.z, 439L, 1639L);
    if ((temp1 > 300*300) && (temp2 > 300*300) && (temp3 > 300*300)){
      return 0;
    }
  }
  return 1;
}

int strongholdSlowCheck(int64_t seed, int fortressQuadrant, LayerStack* gp){
  StrongholdIter sh;
  int mc = MC_1_16;
  Pos pos_sh = initFirstStronghold(&sh, mc, seed);
  long sh_dist = 0xffffffffffff;
  long temp = 0;
  int i, N = 3;
  Pos best_sh;
  for (i = 1; i <= N; i++)
  {
    if (nextStronghold(&sh, gp, NULL) <= 0)
        break;
      if (fortressQuadrant == -1){
        temp = l2norm(sh.pos.x, sh.pos.z, -1200L, 1200L);
        if (temp < sh_dist){
          sh_dist = temp;
          best_sh  = sh.pos;
        }
      } else if (fortressQuadrant == 1){
        temp = l2norm(sh.pos.x, sh.pos.z, 1200L, -1200L);
        if (temp < sh_dist){
          sh_dist = temp;
          best_sh = sh.pos;
        }
      }
  }
  if (sh_dist > 300*300){
    return 0;
  }
  int shbiome = getBiomeAtPos(gp, best_sh);
  if (shbiome != deep_ocean && shbiome != deep_warm_ocean && shbiome != deep_lukewarm_ocean && shbiome != deep_cold_ocean && shbiome != deep_frozen_ocean){
    return 0;
  }
  return 1;
}

int valid_shipwreck_and_ravine_not_biome(int64_t lower48){
  if (ravinePositionAndQuality(lower48) == 0){
    return -7;
  }
  if (shipwreckLocationAndType(lower48) == 0){
    return -8;
  }
  return 1;
}

int valid_jungle_not_biome(int64_t lower48){
  if (jungleLocation(lower48) == 0){
    return -9;
  }
  if (portalTypeJungle(lower48) == 0){ //doing this now biome independent
    return -10;
  }
  return 1;
}

int portalLoot(int64_t lower48, int px, int pz){
  //here we GO
  //set decorator seed: worldSeed, portalPosition.getX() << 4, portalPosition.getZ() << 4, 40005
  int64_t fakeseed = (lower48) ^ 0x5deece66dUL;
  long a = nextLong(&fakeseed) | 1;
  long b = nextLong(&fakeseed) | 1;
  fakeseed = ((long)px * a + (long)pz * b) ^ lower48;
  //fakeseed = fakeseed ^ 0x5deece66dUL;
  fakeseed = fakeseed & 0xffffffffffff; //population seed set?
  int64_t cseed = (fakeseed + 40005) ^ 0x5deece66dUL;//decorator set?
  long chesti = nextLong(&cseed);
  cseed = (int64_t) chesti^ 0x5deece66dUL; //& 0xffffffffffff;// ^ 0x5deece66dUL;//?  IF THINGS ARE WRONG TRY THIS FIRST
  int num_rolls = 4 + nextInt(&cseed, 5);
  int ri = 0;
  int table[100] = {40, 1, 1, 2, 40, 1, 1, 4, 40, 1, 9, 18, 40, 0, 0, 0, 40, 0, 0, 0, 15, 0, 0, 0, 15, 1, 4, 24, 15, 2, 0, 0, 15, 2, 1, 0, 15, 2, 2, 0, 15, 2, 3, 0, 15, 2, 4, 0, 15, 2, 5, 0, 15, 2, 6, 0, 15, 2, 7, 0, 15, 2, 8, 0, 5, 1, 4, 12, 5, 0, 0, 0, 5, 0, 0, 0, 5, 1, 4, 12, 5, 0, 0, 0, 5, 1, 2, 8, 1, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 2};
  int etable[45] = {10, 8, 9, -1, -1, 9, 4, 7, 8, -1, 6, 1, 4, 5, -1, 6, 1, 4, 5, -1, 6, 1, 4, 5, -1, 13, 8, 11, 12, -1, 9, 5, 7, 8, -1, 11, 5, 7, 9, 10, 9, 5, 7, 8, -1};
  int itemi, rv, r_class, i_count, e_roll;
  int enti = -1;
  int flintc = 0, obic = 0, ironc = 0, goldc = 0;//counts in nuggets
  int fireb = 0, finishb = 0, lootb = 0;
  for(ri = 0; ri < num_rolls; ri++){
    rv = nextInt(&cseed, 398);
    //printf("rv: %d\n", rv);
    for(itemi = 0; rv > 0; ){
      rv = rv - table[4*itemi];
      if (rv >= 0){
        itemi++;
      }
    }
    //printf("I think item %d was itemi %d\n", ri, itemi);
    r_class = table[4*itemi + 1];
    if (r_class == 0){
      i_count = 1;
      if (itemi == 3 || itemi == 4){
        fireb = 1;
      }
      continue;
    }
    //printf("continue?\n");
    if (r_class == 1){
      i_count = table[4*itemi + 2] + nextInt(&cseed, table[4*itemi + 3]-table[4*itemi + 2] + 1);
      //printf("item %d class 1 count: %d\n", ri, i_count);
      if (itemi == 0){
        obic += i_count;
      }
      if (itemi == 1){
        flintc += i_count;
      }
      if (itemi == 2){
        ironc += i_count;
      }
      if (itemi == 6){
        goldc += i_count;
      }
      if (itemi == 21){
        goldc += 9*i_count;
      }
      if (itemi == 24){
        goldc += 81*i_count;
      }
      continue;
    }

    if (r_class == 2){
      enti = 5*table[4*itemi + 2];
      e_roll = nextInt(&cseed, etable[enti]);
      //printf("e_roll %d\n", e_roll);
      if (e_roll != etable[enti+1] && e_roll != etable[enti+2] && e_roll != etable[enti+3] && e_roll != etable[enti+4]){
        int elevel = nextInt(&cseed, 3) + 1;
        if (itemi == 7 && e_roll == 5 && elevel >= 2){
          lootb = 1;
        }
      }
    }
  }
  int fitness = 0;
  int ironic = ironc/9;//iron_ingot_count
  if (ironic % 3 != 0){
    if (fireb == 0 && flintc > 0){
      ironic -= 1;
      fireb = 1; //if you can build a flint and steel and it won't drop you out of bucket/pick range then do it
    }
  }
  if (fireb > 0){
    fitness += 2;
  }
  if (lootb > 0){
    fitness += 4;
  }
  if (ironic >= 6){
    fitness += 5;
  } else if (ironic >= 3){
    fitness += 3;
  } else {
    if (goldc >= 36){
      fitness += 1;
    }
  }
  if (obic >= 8){
    fitness += 5;
  }
  if (lootb > 0 && fireb > 0 && ironic >= 1){
    fitness += 100;
  }
  if (fitness >= 80){ // around 5 is 3 iron and flint and steel, around 10 is that plus lots of OBI
    //printf("ironc: %d, obic: %d, flintc: %d, fireb: %d, lootb: %d, goldc: %d\n", ironc, obic, flintc, fireb, lootb, goldc);
    return 1;
  }    
  return 0;
}

int valid_village_and_portal_not_biome(int64_t lower48){
  int px,pz;
  if (portalLocation(lower48, 1, &px, &pz) == 0){ //village special portal
    return -3;
  }
  if (villageLocation(lower48) == 0){
    return -4;
  }
  if (portalTypeNormal(lower48) == 0){ //doing this now biome independent
    return -5;
  }
  if (portalLoot(lower48, px, pz) == 0){
    return -777;
  }

  return 1; //has filtered nether for pos/pos bast and 1 fotress + rough stronghold blind + portal location (the L) + village location (square)
}

int possible_lava(int64_t lower48, int x, int z){
  //printf("lower48: %ld\n", lower48);
  int64_t fakeseed = (lower48) ^ 0x5deece66dUL;
  long a = nextLong(&fakeseed) | 1;
  long b = nextLong(&fakeseed) | 1;
  fakeseed = ((long)x * a + (long)z * b) ^ lower48;
  fakeseed = fakeseed & 0xffffffffffff; //population seed set?
  //printf("population seed: %ld\n", fakeseed);
  int64_t lakeseed = (fakeseed + 10000) ^ 0x5deece66dUL;
  //printf("lakeseed: %ld\n", lakeseed);
  int64_t lavaseed = (fakeseed + 10001) ^ 0x5deece66dUL;
  //printf("lavaseed: %ld\n", lavaseed);
  if (nextInt(&lakeseed, 4) != 0  && nextInt(&lavaseed, 8) != 0){
    return 0;
  }
  nextInt(&lakeseed, 16); //noise in X
  nextInt(&lakeseed, 16); //noise in Z
  nextInt(&lavaseed, 16); //noise in X
  nextInt(&lavaseed, 16); //noise in Z
  
  int lakey = nextInt(&lakeseed, 256);
  int temp = nextInt(&lavaseed, 256 - 8);
  int lavay = nextInt(&lavaseed, temp + 8);

  if (nextInt(&lavaseed, 10) != 0){
    return 0;
  }

  if (lavay > 63 && lakey < 63){
    //printf("lavay %d, lakey %d\n", lavay, lakey);
    return 1;
  }
  return 0;
}

int lava_grid(int64_t lower48){
  int lava_count = 0;
  int cx,cz;
  for(cx = -4; cx < 10; cx++){
    for(cz = -4; cz < 10; cz++){
      lava_count += possible_lava(lower48, cx<<4, cz<<4);
    }
  }
  if (lava_count < 1){
    return 0;
  }
  return 1;
}

int lava_biome(int64_t seed, int x, int z, LayerStack* gp){
  Pos spawn;
  spawn.x = x + 8;
  spawn.z = z + 8;
  int lavabiome = getBiomeAtPos(gp, spawn);
  if (lavabiome == desert){
    return 1;
  }
  return 0;
}

int portalPreLoot(int64_t seed, int* px, int* pz, int* bigsmall, int* pType){
  const StructureConfig sconf_portal = RUINED_PORTAL_CONFIG;
  int valid2;
  Pos p2 = getStructurePos(sconf_portal, seed, 0, 0, &valid2);
  if (!valid2){// || p2.x > 32 || p2.z > 32){
    return 0;
  }
  int portalType;
  int portcx, portcz, portOceanType, portNormalY, portNormalType;
  float buriedFloat, bigOrSmall;
  int rawPortalType;
  int64_t fakeseed, carvea, carveb;
  portcx = p2.x >> 4;
  portcz = p2.z >> 4;
  fakeseed = (seed) ^ 0x5deece66dL;
  carvea = nextLong(&fakeseed);
  carveb = nextLong(&fakeseed);
  fakeseed = ((portcx * carvea) ^ (portcz * carveb) ^ seed) ^ 0x5deece66dL;
  fakeseed = fakeseed & 0xFFFFFFFFFFFF;
  buriedFloat = nextFloat(&fakeseed); // 50/50 shot at being underground
  next(&fakeseed, 31); //tossed out for normal terrains (air gaps)
  bigOrSmall = nextFloat(&fakeseed); // 1/20 chance of being big
  rawPortalType = next(&fakeseed, 31); //once this is reduced mod 3 or 10 we know the type
  if (buriedFloat < .5){
    return 0;
  }
  if (bigOrSmall < .05){
    *bigsmall = 1; //all three big ones have enough lava and we're not underground
    *pType = rawPortalType % 3;
  } else {
    *bigsmall = 0;
    *pType = rawPortalType % 10;
    portalType = rawPortalType % 10;
    if (portalType == 0 || portalType == 2 || portalType == 3 || portalType == 4 || portalType == 5 || portalType == 8){
      return 0; //6 small types have no lava
    }
  }
  *px = p2.x;
  *pz = p2.z;
  //printf("portal at /tp @p %d ~ %d and is of type: %d\n", *px, *pz, *pType);
  return 1;
}

int valid_structures_and_types(int64_t lower48, int* fortressQuadrant, int filter_style){
  if (filter_style == 6){
    int px,pz,bigsmall,pType;
    if (portalPreLoot(lower48, &px, &pz, &bigsmall, &pType) == 0){
      return 0;
    }
    if (portalLoot(lower48, px, pz) == 0){
      return 0;
    }
    return 1;
  }

  if (netherchecker(lower48, fortressQuadrant) == 0){
    return -1;
  }

  if (strongholdAngle(lower48, *fortressQuadrant) == 0){
    return -2;
  }

  if (filter_style == 0){
    return 1; //good nether, good blind, no overworld
  }

  if (filter_style == 1){//village only
    /*if (lava_grid(lower48) == 0){
      return -666;
    }*/
    //filters portal in L village in square and normal portal type
    return valid_village_and_portal_not_biome(lower48); //farmed out so we could do pre-planned or either
  }

  int px, pz;
  if (portalLocation(lower48, 0, &px, &pz) == 0){ //for all future filters
    return -6;
  }

  if (filter_style == 2){//shipwreck
    if (portalTypeNormal(lower48) == 0){ //doing this now biome independent
      return -5;
    }
    if (portalLoot(lower48, px, pz) == 0){
      return -777;
    }
    return valid_shipwreck_and_ravine_not_biome(lower48);
  }

  if (filter_style == 3){//jungle only
    return valid_jungle_not_biome(lower48);
  }
  return 1;
}

int spawn_close(int64_t seed, LayerStack* gp){ //costs about 1/6 biomes...
  int mc = MC_1_16;
  Pos spawn = getSpawn(mc, gp, NULL, seed);
  if (spawn.x >= -48 && spawn.x <= 80 && spawn.z >= -48 && spawn.z <= 80){
    return 1;
  }
  return 0;
}

int spawn_medium(int64_t seed, LayerStack* gp){ //for shipwrecks we're ok being a little farther away for trees
  int mc = MC_1_16;
  Pos spawn = getSpawn(mc, gp, NULL, seed);
  if (spawn.x >= -100 && spawn.x <= 200 && spawn.z >= -100 && spawn.z <= 200){
    return 1;
  }
  return 0;
}

int villageBiome(int64_t seed, LayerStack* gp){
  const StructureConfig sconf = VILLAGE_CONFIG;
  int valid;
  Pos p = getStructurePos(sconf, seed, 0, 0, &valid);
  int mc = MC_1_16;
  if (!isViableStructurePos(sconf.structType, mc, gp, seed, p.x, p.z)){
    return 0;
  }
  int biome = getBiomeAtPos(gp, p);
  if (biome == snowy_tundra || biome == desert){
    return 0;
  }

  return 1;
}

int shipwreckBiome(int64_t seed, LayerStack* gp){
  const StructureConfig sconf_shipwreck = SHIPWRECK_CONFIG;
  int valid3;
  Pos p3 = getStructurePos(sconf_shipwreck, seed, 0, 0, &valid3);
  int mc = MC_1_16;
  if (!isOceanic(getBiomeAtPos(gp, p3))){
    return 0;
  }
  if (!isViableStructurePos(sconf_shipwreck.structType, mc, gp, seed, p3.x, p3.z)){
    return 0;
  }
  return 1;
}

int jungleBiome(int64_t seed, LayerStack* gp){
  const StructureConfig sconf = JUNGLE_PYRAMID_CONFIG;
  int valid;
  Pos p = getStructurePos(sconf, seed, 0, 0, &valid);
  int mc = MC_1_16;
  if (!isViableStructurePos(sconf.structType, mc, gp, seed, p.x, p.z)){
    return 0;
  }
  return 1;
}

int valid_biomes(int64_t seed, int* fortressQuadrant, int filter_style, LayerStack* gp){
  applySeed(gp, seed);
  if (filter_style == 6){

    return 1;
  }

  if (strongholdSlowCheck(seed, *fortressQuadrant, gp) == 0){
    return -20;
  }
  if (filter_style == 0){
    return 1;
  }

  if (portalBiome(seed, gp)==0){
    return -17;
  }
  if (filter_style == 1){
    if (spawn_close(seed, gp) == 0){
      return -11;
    }
    if (villageBiome(seed, gp)== 0){
      return -12;
    }
  }

  if (filter_style == 3){
    if (spawn_close(seed, gp) == 0){
      return -11;
    }
    if (jungleBiome(seed, gp)== 0){
      return -13;
    }
  }

  if (filter_style == 2){
    if (shipwreckBiome(seed, gp)== 0){
      return -14;
    }
    if (spawn_close(seed, gp) == 0){
      return -15;
    }
    if (ravineBiome(seed, gp) == 0){
      return -16;
    }
  }
  return 1;
}

// use this structure to pass around parameters
struct thread_param_t {

	int filterStyle;	// the style of filter to use
  	LayerStack *g;		// save ourselves from re-initing this each biome check
	bool verbose;		// be chatty about what we're doing
	uint32_t biome_reset;	// when should we switch to searching another biome?

	uint64_t start;		// the nonce or starting location
	uint32_t step;		// how many threads there are in total
	uint32_t index;		// this thread's ID

	uint64_t* structs;	// a pointer to thread storage for structure seeds
	uint32_t* biomes;	// thread storage for the biome portion
  	int* fortressQuadrant;  // need this to persist some state from structure to biome

	uint64_t* s_checked;	// per-thread status updates for structure seeds checked
	uint32_t* b_checked;	//   and biome seeds checked

  };

// biome checking
uint32_t find_biome_seed( uint64_t seed, struct thread_param_t* params ) {

  // de-void the parameters and make them local
  struct thread_param_t p = *params;
  
  // figure out the structure seed we're checking
  int fortressQuadrant = p.fortressQuadrant[p.index];

  // set up any additional params
  uint32_t checked = p.b_checked[p.index];
  if( p.verbose )
    printf( "\nDEBUG: thread %u checking the biomes of seed %lu.\n", p.index, seed );

  // for each biome seed
  for( uint64_t biome = 0; biome < p.biome_reset; biome++ ) {

    // check if we need to synchronize
    if( (checked & BIOME_MASK) == BIOME_MASK ) {

      p.b_checked[p.index] = checked;
    
      // for each thread
      for( uint32_t i = 0; i < p.step; i++ ) {

        // other than ourselves
        if( i == p.index )
          continue;

        // check if another thread found a biome
        if( p.biomes[i] <= 0xffff )
	
	  if ( (p.structs[i] < p.structs[p.index]) || ((p.structs[i] == p.structs[p.index]) && (p.biomes[i] < biome)) ) {

            if( p.verbose )
       	      printf( "\nDEBUG: thread %u is quitting, thread %u found a biome.\n", p.index, i );

            return 0x10000;
	    }

        } // for (each thread)
      } // if (time to sync)

    // otherwise, check the current biome
    int result = valid_biomes(seed + (biome << 48), &fortressQuadrant, p.filterStyle, p.g);
    checked++;

    // success? Terminate!
    if( result > 0 ) {

      if( p.verbose )
        printf( "\nDEBUG: thread %u is quitting, found a biome at %lu.\n", p.index, biome );
      p.biomes[p.index] = biome;
      p.b_checked[p.index] = checked;
      return biome;
      }

    } // for (each biome seed)

  // otherwise, exit in shame
  p.biomes[p.index] = 0xffffffff;
  p.b_checked[p.index] = checked;
  return 0xffffffff;
  }

void* find_seed( void* params ) {

  // de-void the parameters and make them local
  struct thread_param_t p = *(struct thread_param_t*) params;
	
  // create the rest of the parameters we'll need
  int fortressQuadrant = 0;
  uint64_t checked = p.s_checked[p.index];

  if( p.verbose )
    printf( "\nDEBUG: thread %u starting at %lu.\n", p.index, p.start );

  // for each structure seed
  for( uint64_t s_idx = checked; s_idx <= 0xffffffffffffL; s_idx++ ) {

    // check if we need to synchronize
    if( (checked & STRUCT_MASK) == STRUCT_MASK ) {

      // update our stats
      p.s_checked[p.index] = checked;

      // for each thread
      for( uint32_t i = 0; i < p.step; i++ ) {

        // except ourselves
        if( i == p.index )
          continue;

        // check if they've terminated with a better seed
        if( (p.biomes[i] <= 0xffff) && (p.structs[i] < s_idx) ) {

          if( p.verbose )
            printf( "\nDEBUG: thread %u quitting, because thread %u found a seed.\n", p.index, i );

          pthread_exit(NULL);		// time to give up, then
	  }

        } // for (each thread)
      } // if (time to check)

    // otherwise, we check the current seed
    uint64_t seed = p.start + s_idx;

    int result = valid_structures_and_types( seed & 0xffffffffffffL, &fortressQuadrant, 
        p.filterStyle );

    checked++;

    // no valid structures? Bad bastion biome? Try a different structure seed
    if ((result <= 0) || (bastionbiome(seed) <= 0))
        continue;

    // success! Ask for a biome seed
    if( p.verbose )
      printf( "\nDEBUG: thread %u found a seed at offset %lu.\n", p.index, s_idx );

    p.s_checked[p.index] = checked;
    p.fortressQuadrant[p.index] = fortressQuadrant;
    uint32_t retVal = find_biome_seed( seed, &p );

    // did we find a legit biome?
    if( retVal <= 0xffff ) {

      p.structs[p.index] = s_idx;
      p.biomes[p.index] = retVal;
      }

    // were we told to exit?
    if( retVal <= 0x10000 )
      pthread_exit(NULL);

    // otherwise, carry on

    } // for( each seed )

  } // find_seed

void check_seed( uint64_t seed, int filterStyle, LayerStack* g ) {

  // we need some init-ing
  int fortressQuadrant = 0;

  // then just carry out the tests
  int result = valid_structures_and_types( seed & 0xffffffffffffL, &fortressQuadrant, 
    filterStyle );
  printf( "%ld\tvalid_structures_and_types: %d", seed, result );

  result = bastionbiome( seed );
  printf( "\tbastionbiome: %d", result );

  result = valid_biomes(seed, &fortressQuadrant, filterStyle, g);
  printf( "\tvalid_biomes: %d\n", result );

  fflush(stdout);
  }

// use this outside the inner loop for book-keeping
struct token_t {
        
	uint8_t version;	// what version of the filters are in use
	uint8_t filterStyle;	// the specific filter being used
	uint64_t nonce;		// the nonce used as a starting point
	uint64_t seed;		// the seed that was discovered
	uint64_t structs;	// the number of structure seeds examined to get there
	uint64_t biomes;	// the number of biome variants examined to get there
	uint64_t timestamp;	// the time the search started
  };

void print_verification_token( struct token_t* token ) {

  if( token == NULL )
    return;

  printf("Verification Token:\n");
  printf("%02hhX%02hhX-%016lx-%016lX-%lx-%lx-%lX\n", token->version, token->filterStyle, token->nonce, 
    token->seed, token->structs + token->biomes, token->biomes, token->timestamp);

  }

// NOTE: don't forget to free() the return!
struct token_t* parse_verification_token( unsigned char* str ) {

  struct token_t* token = malloc( sizeof(struct token_t) );

  int retVal = sscanf( str, "%02hhX%02hhX-%016lx-%016lX-%lx-%lx-%lX", &(token->version), &(token->filterStyle),
    &(token->nonce), &(token->seed), &(token->structs), &(token->biomes), &(token->timestamp) );

  if( retVal != 7 ) {

    free( token );
    return NULL;
    }

  token->structs -= token->biomes;	// adjust the structure seed count
  return token;
  }

int main (int argc, char** argv) {

  // read the command line params
  uint32_t threads	= 0;
  int filterStyle	= 1;
  bool verbose		= false;
  struct token_t* verify= NULL;

  initBiomes();		// in case we're asked to check
  LayerStack g;
  setupGenerator(&g, MINECRAFT_VERSION);

  char descriptions[5][65] = {"RSG overworld", "Village Only", "Shipwreck Only", 
	  "Jungle Only", ""};
  uint32_t biome_cutoffs[5] = {1269, 15520, 2744, 5456, 0};

  static struct option cmdline_options[] = {
    {"check",   required_argument, 0, 'c' },
    {"filter",  required_argument, 0, 'f' },
    {"threads", required_argument, 0, 't' },
    {"verbose", no_argument,       0, 'v' },
    {"help",    no_argument,       0, 'h' },
    {"Verify",  required_argument, 0, 'V' },
    {0,         0,                 0,  0 }
    };

  while( true ) {

    int optidx = -1;
    int c      = getopt_long(argc, argv, "c:f:t:V:vh", cmdline_options, &optidx);
    if (c == -1)
      break;

    switch( c ) {

      case 'h':		// print out help
	printf("FSG Seed Filter v%d\n", VERSION);
	printf("\n");
	printf(" USAGE: %s [--help] [--verbose] [--threads THREADS] [--filter FILTER]\n", argv[0]);
	printf("          [--check SEED]\n");
	printf("\n");
	printf(" --help:    Print this message.\n");
	printf(" --verbose: Print out additional information while running.\n");
	printf(" --threads: Set the number of threads to use. Defaults to 0, or use all\n");
	printf("             available.\n");
	printf(" --check:   Print the weights each interior filter applies to SEED, a\n");
	printf("             Minecraft seed.\n");
	printf(" --filter:  Use the given filter when evaluating seeds. Choices are:\n");
        for( uint i = 0; i < 4; i++ )
          printf("             %3d: %s\n", i, descriptions[i] );
	printf("\n");
	printf(" Each long option has a short version consisting of their first letter.\n");
	printf("   For instance, \"-v\" is identical to \"--verbose\".\n");
	printf("\n");
	return 0;

      case 'c':		// check a seed
	printf("FSG Seed Filter v%d\n", VERSION);
	printf("\n");
        check_seed( strtol(optarg, NULL, 0), filterStyle, &g );
        return 0;

      case 'f':		// choice of filter

        filterStyle = strtol( optarg, NULL, 0 );
        if( (filterStyle < 0) || (filterStyle > 3) ) {

          printf( "ERROR: Invalid filter type! Only the following are accepted:\n" );
          for( uint i = 0; i < 4; i++ )
            printf( "\t%d = %s\n", i, descriptions[i] );

          return 1;
          }

        break;

      case 't':		// number of threads

        threads = strtol( optarg, NULL, 0 );
        break;		// defer on error checking until later

      case 'v':		// be verbose
        verbose = true;
	break;
     
      case 'V':		// verify mode
        verify = parse_verification_token( optarg );
        if( verify != NULL ) {

	  // do some preliminary checking
	  if( verify->version != VERSION ) {

	    printf( "ERROR: The FSG version used to generate this token, %d, cannot be checked with this program (which is version %d).\n",
		 verify->version, VERSION );
	    return 1;
	    }
	  filterStyle	= verify->filterStyle;
	  }

	break;

      } // switch
    } // while( command line options )

  uint32_t num_procs = sysconf(_SC_NPROCESSORS_ONLN);
  if( (threads == 0) || (threads > num_procs) )
    threads = num_procs;
  if( verify != NULL )
    threads = 1;		// verification is single-threaded

  // annouce ourselves
  printf( "FSG v%d %s, %d threads\n", VERSION, descriptions[filterStyle], threads );

  // do additional setup
  uint64_t nonce = rand64();
  if( verify != NULL )
    nonce = verify->nonce;

  struct timespec now;		// need wall clock time for the token
  clock_gettime( CLOCK_REALTIME, &now );
  uint64_t now_ticks = (uint64_t)(now.tv_sec*1e6 + now.tv_nsec*1e-3);
  // "now" is free to be recycled after this point

  struct timespec start;	// need a more accurate time for the stats
  clock_gettime( CLOCK_MONOTONIC, &start );

  // create a parameter football for each thread, then launch
  pthread_t thread_id[threads];
  struct thread_param_t params[threads];
  LayerStack* g_pool = malloc( sizeof(LayerStack) * threads );
  uint64_t nonce_step = 0xffffffffffffL / threads;		// close enough

  for( uint32_t i = 0; i < threads; i++ ) {

    params[i].verbose     = verbose;
    params[i].filterStyle = filterStyle;
    params[i].g           = g_pool + i;
    params[i].biome_reset = biome_cutoffs[filterStyle];

    params[i].start       = nonce + i*nonce_step;
    params[i].step        = threads;
    params[i].index       = i;
  
    if( i == 0 ) {

      params[i].fortressQuadrant = malloc( sizeof(int) * threads );
      params[i].structs   = malloc( sizeof(uint64_t) * threads );
      params[i].s_checked = malloc( sizeof(uint64_t) * threads );
      params[i].biomes    = malloc( sizeof(uint32_t) * threads );
      params[i].b_checked = malloc( sizeof(uint32_t) * threads );
      }
    else {

      params[i].fortressQuadrant = params[0].fortressQuadrant;
      params[i].structs   = params[0].structs;
      params[i].s_checked = params[0].s_checked;
      params[i].biomes    = params[0].biomes;
      params[i].b_checked = params[0].b_checked;
      }
	
    setupGenerator(params[i].g, MINECRAFT_VERSION);
    params[0].fortressQuadrant[i]   = 0;
    params[0].structs[i]   = 0xffffffffffffffffL;
    params[0].s_checked[i] = 0;
    params[0].biomes[i]    = 0xffffffff;
    params[0].b_checked[i] = 0;

    int results = pthread_create( thread_id + i, NULL, find_seed, params + i );
    if( results != 0 ) {

       printf( "ERROR! Could not create a thread, was given error code %d.\n",
	results );
       return 2;
       }

    } // for (each thread)

  // enter the status loop
  struct timespec delay = {0, 1e3}; // sleep for 1ms

  uint64_t las_structures = 0;
  uint32_t las_biomes = 0;
  struct timespec last;		// used for status updates
  clock_gettime( CLOCK_MONOTONIC, &last );

  uint32_t best_seed = threads;

  while( true ) {

    nanosleep( &delay, NULL );  // ignore interruptions and errors

    uint64_t tot_structures = 0;
    uint32_t tot_biomes = 0;

    // check if anyone's quit
    for( uint32_t i = 0; i < threads; i++ )
      if( params[0].biomes[i] <= 0xffff )
        best_seed = i;			// (not necessarily the best here!)
    
    if( best_seed < threads ) {

      // force all threads to quit
      for( uint32_t i = 0; i < threads; i++ )
        pthread_join( thread_id[i], NULL );

      // now find the ACTUAL best
      for( uint32_t i = 0; i < threads; i++ )
        if( (params[0].biomes[i] <= 0xffff) && (i != best_seed) ) {

          if( params[0].structs[i] < params[0].structs[best_seed] )
	    best_seed = i;
          else if( (params[0].structs[i] == params[0].structs[best_seed]) && 
	        (params[0].biomes[i] < params[0].biomes[best_seed]) )
            best_seed = i;
          }

      // tally up stats and print the token
      uint64_t seed = params[best_seed].start + params[0].structs[best_seed] + 
          ((uint64_t)params[0].biomes[best_seed] << 48);

      for( uint32_t i = 0; i < threads; i++ ) {

        tot_structures += params[0].s_checked[i];
        tot_biomes     += params[0].b_checked[i];
        }

      // if we're in verification mode, verify the correct seed was found
      if( verify != NULL ) {

	  if( seed == verify->seed )
  	    printf("\nGOOD: The terminating seed is what we expected.\n");
	  else
  	    printf("\nBAD: The terminating seed was NOT the same!\n");

	  if( params[0].structs[best_seed] == verify->structs )
  	    printf("GOOD: The number of structure seeds checked matches.\n");
	  else
  	    printf("BAD: The number of structure seeds checked does NOT match.\n");

	  if( params[0].biomes[best_seed] == verify->biomes )
  	    printf("GOOD: The number of biome seeds checked matches.\n");
	  else
  	    printf("BAD: The number of biome seeds checked does NOT match.\n");

        }
      else {

        printf("\nSeed: %ld\n", seed);
        printf("Filtered %lu seeds did %u biome checks\n", tot_structures+tot_biomes, 
          tot_biomes );

        clock_gettime( CLOCK_MONOTONIC, &last );

	// recycle this variable
	verify = malloc( sizeof(struct token_t) );
	verify->version 	= VERSION;
	verify->filterStyle 	= filterStyle;
	verify->nonce 		= params[best_seed].start;
	verify->seed 		= seed;
	verify->structs		= params[0].structs[best_seed];
	verify->biomes		= params[0].biomes[best_seed];
	verify->timestamp	= now_ticks;

        print_verification_token( verify );
        }

      uint64_t delta = (last.tv_sec*1e9 + last.tv_nsec) - (start.tv_sec*1e9 + start.tv_nsec);
      printf("Time to complete in nanoseconds: %lu\n", delta );
      fflush(stdout);

      free( g_pool );
      free( params[0].fortressQuadrant );
      free( params[0].structs );
      free( params[0].s_checked );
      free( params[0].biomes );
      free( params[0].b_checked );
      free( verify );
    
      return 0;
      }

    // otherwise, check if it's time for a status display
    clock_gettime( CLOCK_MONOTONIC, &now );
    if( now.tv_sec != last.tv_sec ) {

      // tally up stats and try to find the best structure seed
      best_seed = threads;
      for( uint32_t i = 0; i < threads; i++ ) {

        tot_structures += params[0].s_checked[i];
        tot_biomes     += params[0].b_checked[i];

        if( params[0].structs[i] <= 0xffffffffffffL ) {

	  if( (best_seed == threads) || (params[0].structs[i] < params[0].structs[best_seed]) )
            best_seed = i;
	  }

        }

      printf( "\rSTATUS: %lu structs, %u biomes, ", tot_structures, tot_biomes );

      uint64_t delta = (now.tv_sec*1e9 + now.tv_nsec) - (last.tv_sec*1e9 + last.tv_nsec);
      float per_sec  = (float) ((tot_structures - las_structures) + 
          (tot_biomes - las_biomes)) * 1e9 / (float) delta;
      if( per_sec < 1000 )
        printf( "%.1f checks/s", per_sec );
      else if( per_sec < 1e6 )
    	  printf( "%.1fk checks/s", per_sec * 1e-3 );
      else if( per_sec < 1e9 )
    	  printf( "%.1fM checks/s", per_sec * 1e-6 );
      else 
    	  printf( "%.1fG checks/s", per_sec * 1e-9 );

      if( best_seed < threads )
    	  printf( ", testing %ld", (params[best_seed].start + params[0].structs[best_seed]) & 0xffffffffffffL );
      else
    	  printf( "          %20s", "" );

      printf( "   " );	// print out some spaces to overwrite any unusually long values
      fflush(stdout);   //  but not too many, otherwise it may wrap

      las_structures = tot_structures;
      las_biomes = tot_biomes;
      last = now;

      } // if( status display )

    } // while( infinity )

  return -1;	// we should never reach this

  } // main
