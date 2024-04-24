#ifndef __PROGTEST__

#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cassert>
using namespace std;

constexpr int                          SECTOR_SIZE                             =             512;
constexpr int                          MAX_RAID_DEVICES                        =              16;
constexpr int                          MAX_DEVICE_SECTORS                      = 1024 * 1024 * 2;
constexpr int                          MIN_DEVICE_SECTORS                      =    1 * 1024 * 2;

constexpr int                          RAID_STOPPED                            = 0;
constexpr int                          RAID_OK                                 = 1;
constexpr int                          RAID_DEGRADED                           = 2;
constexpr int                          RAID_FAILED                             = 3;

struct TBlkDev
{
	int m_Devices;
	int m_Sectors;
	int (* m_Read)( int, int, void *, int );
	int (* m_Write) ( int, int, const void *, int );
};
#endif /* __PROGTEST__ */

class CRaidVolume
{
	public:
		TBlkDev m_Dev;
		int m_status = RAID_STOPPED;
		int config_size = 1;// sector
		int degraded_disk = -1;
		struct DiskState {
			int touched;
			int disk_index;
			int m_status;
			int degraded_disk;
			DiskState(int i):touched(-1),disk_index(i),m_status(RAID_OK),degraded_disk(-1){}
		};



		static bool create (const TBlkDev& dev ){
			if(sizeof(TBlkDev) > SECTOR_SIZE){
				return false;
			}
			char* data = new char[SECTOR_SIZE];
			bool success = true;
			for(int i = 0; i < dev.m_Devices; i++){
				DiskState state(i);
				memset(data, 0, SECTOR_SIZE);
				memcpy(data, &state, sizeof(DiskState));
				int ret = dev.m_Write(i, 0, data, 1);
				if(ret != 1) success = false;
			}
			delete [] data;
			return success;
		}

		int start (const TBlkDev& dev){
			if(m_status != RAID_STOPPED){
				return m_status;
			}
			char* data = new char[SECTOR_SIZE];
			m_Dev = dev;
			int count = 0;
			bool* read_success = new bool[m_Dev.m_Devices];
			for(int i = 0; i < m_Dev.m_Devices; i++){
				read_success[i] = false;
			}
			int* degrade_info = new int[m_Dev.m_Devices];
			int* raid_state = new int[m_Dev.m_Devices];

			for(int i = 0; i < m_Dev.m_Devices; i++){
				if(dev.m_Read(i, 0, data, 1) != 1) continue;
				
				DiskState state(i);
				memcpy(&state, data, sizeof(DiskState));
				if(state.touched != -1) continue;
				count++;
				read_success[i] = true;
				degrade_info[i] = state.degraded_disk;
				raid_state[i] = state.m_status;
			}

			delete [] data;

			for(int i = 0; i < m_Dev.m_Devices; i++){
				if(!read_success[i]) continue;
				if(degraded_disk == i || degrade_info[i] == i) continue;
				degraded_disk = degrade_info[i];// can error here
				m_status = raid_state[i]; // can error here
			}

			delete [] degrade_info;
			delete [] raid_state;
			delete [] read_success;

			if(count < m_Dev.m_Devices-1){
				m_status = RAID_FAILED;
			}
			return m_status;
		}
		int stop(){
			
		}
		int resync (){

		}
		int status() const{
			return m_status;
		}
		int size() const {
			return (m_Dev.m_Devices - 2) * (m_Dev.m_Sectors-1);
		}

		bool read (int secNr, void* data, int secCnt){
			if(m_status != RAID_OK && m_status != RAID_DEGRADED){
				return false;
			}
			if(secNr < 0 || secCnt <= 0 || secNr + secCnt > size() ){
				return false;
			}
			int startingSector = secNr / (m_Dev.m_Devices - 2) + config_size;
            int startingDisk = secNr % (m_Dev.m_Devices - 2);
            int parity_at_start_disk = (startingSector) % m_Dev.m_Devices;
            if(startingDisk >= parity_at_start_disk){
                startingDisk++;
            }
            int endingSector = (secNr+secCnt-1) / (m_Dev.m_Devices - 2) + config_size;
            int endingDisk = (secNr+secCnt-1) % (m_Dev.m_Devices - 2);
            int parity_at_end_disk = (endingSector) % m_Dev.m_Devices;
            if(endingDisk >= parity_at_end_disk){
                endingDisk++;
            }
            int row_size = endingSector - startingSector + 1;

			char** buffers = new char*[m_Dev.m_Devices];
            for (int i = 0; i < m_Dev.m_Devices; i++) {
                buffers[i] = new char[SECTOR_SIZE * row_size];
            }


			for(int j = 0; j < m_Dev.m_Devices; j++){
                int ret = m_Dev.m_Read(j, startingSector, buffers[j], row_size);
                if(ret != row_size){
                    if(m_status == RAID_DEGRADED){
                        if(degraded_disk != j){
                            m_status = RAID_FAILED;
                            for(int d = 0; d < m_Dev.m_Devices; d++){
                                delete[] buffers[d];
                            }
                            delete[] buffers;
                            return false;
                        }
                    }else if(m_status == RAID_OK){
                        m_status = RAID_DEGRADED;
                        degraded_disk = j;
                    }
                }
            }



		}
		bool write (int secNr, const void* data, int secCnt){

		}
  	protected:
    // todo
};

#ifndef __PROGTEST__
constexpr int                          RAID_DEVICES                            = 4;
constexpr int                          DISK_SECTORS                            = 8192;
static FILE                          * g_Fp[RAID_DEVICES];

//-------------------------------------------------------------------------------------------------
/** Sample sector reading function. The function will be called by your Raid driver implementation.
 * Notice, the function is not called directly. Instead, the function will be invoked indirectly
 * through function pointer in the TBlkDev structure.
 */
int                                    diskRead                                ( int                                   device,
                                                                                 int                                   sectorNr,
                                                                                 void                                * data,
                                                                                 int                                   sectorCnt )
{
  if ( device < 0 || device >= RAID_DEVICES )
    return 0;
  if ( g_Fp[device] == NULL )
    return 0;
  if ( sectorCnt <= 0 || sectorNr + sectorCnt > DISK_SECTORS )
    return 0;
  fseek ( g_Fp[device], sectorNr * SECTOR_SIZE, SEEK_SET );
  return fread ( data, SECTOR_SIZE, sectorCnt, g_Fp[device] );
}
//-------------------------------------------------------------------------------------------------
/** Sample sector writing function. Similar to diskRead
 */
int                                    diskWrite                               ( int                                   device,
                                                                                 int                                   sectorNr,
                                                                                 const void                          * data,
                                                                                 int                                   sectorCnt )
{
  if ( device < 0 || device >= RAID_DEVICES )
    return 0;
  if ( g_Fp[device] == NULL )
    return 0;
  if ( sectorCnt <= 0 || sectorNr + sectorCnt > DISK_SECTORS )
    return 0;
  fseek ( g_Fp[device], sectorNr * SECTOR_SIZE, SEEK_SET );
  return fwrite ( data, SECTOR_SIZE, sectorCnt, g_Fp[device] );
}
//-------------------------------------------------------------------------------------------------
/** A function which releases resources allocated by openDisks/createDisks
 */
void                                   doneDisks                               ()
{
  for ( int i = 0; i < RAID_DEVICES; i ++ )
    if ( g_Fp[i] )
    {
      fclose ( g_Fp[i] );
      g_Fp[i]  = NULL;
    }
}
//-------------------------------------------------------------------------------------------------
/** A function which creates the files needed for the sector reading/writing functions above.
 * This function is only needed for the particular implementation above.
 */
TBlkDev                                createDisks                             ()
{
  char       buffer[SECTOR_SIZE];
  TBlkDev    res;
  char       fn[100];

  memset    ( buffer, 0, sizeof ( buffer ) );

  for ( int i = 0; i < RAID_DEVICES; i ++ )
  {
    snprintf ( fn, sizeof ( fn ), "/tmp/%04d", i );
    g_Fp[i] = fopen ( fn, "w+b" );
    if ( ! g_Fp[i] )
    {
      doneDisks ();
      throw "Raw storage create error";
    }

    for ( int j = 0; j < DISK_SECTORS; j ++ )
      if ( fwrite ( buffer, sizeof ( buffer ), 1, g_Fp[i] ) != 1 )
      {
        doneDisks ();
        throw "Raw storage create error";
      }
  }

  res . m_Devices = RAID_DEVICES;
  res . m_Sectors = DISK_SECTORS;
  res . m_Read    = diskRead;
  res . m_Write   = diskWrite;
  return res;
}
//-------------------------------------------------------------------------------------------------
/** A function which opens the files needed for the sector reading/writing functions above.
 * This function is only needed for the particular implementation above.
 */
TBlkDev                                openDisks                               ()
{
  TBlkDev    res;
  char       fn[100];

  for ( int i = 0; i < RAID_DEVICES; i ++ )
  {
    snprintf ( fn, sizeof ( fn ), "/tmp/%04d", i );
    g_Fp[i] = fopen ( fn, "r+b" );
    if ( ! g_Fp[i] )
    {
      doneDisks ();
      throw "Raw storage access error";
    }
    fseek ( g_Fp[i], 0, SEEK_END );
    if ( ftell ( g_Fp[i] ) != DISK_SECTORS * SECTOR_SIZE )
    {
      doneDisks ();
      throw "Raw storage read error";
    }
  }
  res . m_Devices = RAID_DEVICES;
  res . m_Sectors = DISK_SECTORS;
  res . m_Read    = diskRead;
  res . m_Write   = diskWrite;
  return res;
}
//-------------------------------------------------------------------------------------------------
void                                   test1                                   ()
{
  /* create the disks before we use them
   */
  TBlkDev  dev = createDisks ();
  /* The disks are ready at this moment. Your RAID-related functions may be executed,
   * the disk backend is ready.
   *
   * First, try to create the RAID:
   */

  assert ( CRaidVolume::create ( dev ) );


  /* start RAID volume */

  CRaidVolume vol;

  assert ( vol . start ( dev ) == RAID_OK );
  assert ( vol . status () == RAID_OK );

  /* your raid device shall be up.
   * try to read and write all RAID sectors:
   */

  for ( int i = 0; i < vol . size (); i ++ )
  {
    char buffer [SECTOR_SIZE];

    assert ( vol . read ( i, buffer, 1 ) );
    assert ( vol . write ( i, buffer, 1 ) );
  }

  /* Extensive testing of your RAID implementation ...
   */


  /* Stop the raid device ...
   */
  assert ( vol . stop () == RAID_STOPPED );
  assert ( vol . status () == RAID_STOPPED );

  /* ... and the underlying disks.
   */

  doneDisks ();
}
//-------------------------------------------------------------------------------------------------
void                                   test2                                   ()
{
  /* The RAID as well as disks are stopped. It corresponds i.e. to the
   * restart of a real computer.
   *
   * after the restart, we will not create the disks, nor create RAID (we do not
   * want to destroy the content). Instead, we will only open/start the devices:
   */

  TBlkDev dev = openDisks ();
  CRaidVolume vol;

  assert ( vol . start ( dev ) == RAID_OK );


  /* some I/O: RaidRead/RaidWrite
   */

  vol . stop ();
  doneDisks ();
}
//-------------------------------------------------------------------------------------------------
int                                    main                                    ()
{
  test1 ();
  test2 ();
  return EXIT_SUCCESS;
}

#endif /* __PROGTEST__ */
