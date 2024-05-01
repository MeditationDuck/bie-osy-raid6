#ifndef __PROGTEST__
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cassert>
#include <stdexcept>
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
    int                                  m_Devices;
    int                                  m_Sectors;

    /**
     * @brief m_Read 
     * int deviceNr
     * int sectorNr
     * const void* data
     * int sectorCnt
    */
    int                               (* m_Read )  ( int, int, void *, int );
    /**
     * @brief m_Write 
     * int deviceNr
     * int sectorNr
     * const void* data
     * int sectorCnt
    */
    int                               (* m_Write ) ( int, int, const void *, int );
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


        static bool create(const TBlkDev& dev){
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
        int start(const TBlkDev& dev){
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
            if(m_status == RAID_STOPPED){
                return m_status;
            }
            
            if(m_status == RAID_OK || m_status == RAID_DEGRADED){
                char* data = new char[SECTOR_SIZE];
                for(int i = 0; i < m_Dev.m_Devices;i++){
                    // if(m_status == RAID_DEGRADED && i == degraded_disk) continue;
                    DiskState state(i);
                    state.degraded_disk = degraded_disk;
                    state.m_status = m_status;
                    memset(data, 0, SECTOR_SIZE);
                    memcpy(data, &state, sizeof(DiskState));
                    int ret = m_Dev.m_Write(i, 0, data, 1);
                    if(ret != 1 && m_status == RAID_DEGRADED && i != degraded_disk){
                        m_status = RAID_FAILED;
                    }
                }
                delete [] data;
            }

            if(m_status == RAID_FAILED){
                char* data = new char[SECTOR_SIZE];
                for(int i = 0; i < m_Dev.m_Devices;i++){
                    DiskState state(i);
                    state.m_status = RAID_FAILED;
                    memset(data, 0, SECTOR_SIZE);
                    memcpy(data, &state, sizeof(DiskState));
                    m_Dev.m_Write(i, 0, data, 1);
                }
                delete [] data;
            }

            m_status = RAID_STOPPED;
            return m_status;
        }

        int resync(){
            if(m_status != RAID_DEGRADED){
                return m_status;
            }

            // DiskState state(degraded_disk);
            // char* data = new char[SECTOR_SIZE];
            // memset(data, 0, SECTOR_SIZE);
            // memcpy(data, &state, sizeof(DiskState));

            // int ret = m_Dev.m_Write(degraded_disk, 0, data, 1);
            // if(ret != 1){
            //     m_status = RAID_DEGRADED;
            //     return m_status;
            // }


            int granurity = 16;
            char* buffer = new char[SECTOR_SIZE * granurity];
            char* recover = new char[SECTOR_SIZE * granurity];
            int sector_curr = config_size;
            int size_of_drive = m_Dev.m_Sectors;

            char** buffers;
            initBuffers(buffers, granurity);
            
            while(sector_curr < size_of_drive){
                if(size_of_drive - sector_curr < granurity){
                    granurity = size_of_drive - sector_curr;
                }

                if(readDiskData(buffers, sector_curr, granurity) == false){
                    deleteBuffers(buffers);
					return m_status;
                }

                fixDatas(buffers, granurity);
                writeDiskData(buffers, sector_curr, granurity);
                sector_curr += granurity;
            }

            m_status = RAID_OK;
            delete [] buffer;
            delete [] recover;
            return m_status;
        }

        int status() const{
            return m_status;
        }

        int size() const {
            return (m_Dev.m_Devices - 1) * (m_Dev.m_Sectors-1);
        }


        int parityDisk(int row){
            return row % m_Dev.m_Devices;
        }

        bool read(int secNr, void* data, int secCnt){
            if(m_status != RAID_OK && m_status != RAID_DEGRADED){
                return false;
            }
            if(secNr < 0 || secCnt <= 0 || secNr + secCnt > size() ){
                return false;
            }
            int startingSector, startingDisk, endingSector, endingDisk,row_size;
            calculateIndexes(secNr, secCnt, 
                startingSector, startingDisk, endingSector, endingDisk, row_size
            );
            char** buffers;
    
            initBuffers(buffers, row_size);

            if(readDiskData(buffers, startingSector, row_size) == false){
                deleteBuffers(buffers);
                return false;
            }
            // only there was error then check parity. and if necessary, fix disk.
            fixDatas(buffers,row_size);

            int data_index = 0;
            char* dataPtr = static_cast<char*>(data);

            for(int i = 0; i < row_size; i++){
                int parity_disk = (startingSector + i) % m_Dev.m_Devices;
                for(int j = 0; j < m_Dev.m_Devices; j++){
                    if(j == parity_disk) continue;
                    if((i == 0 && j < startingDisk) || (i == row_size - 1 && j > endingDisk)) continue;
                    memcpy(dataPtr + (data_index * SECTOR_SIZE), buffers[j] + (i * SECTOR_SIZE), SECTOR_SIZE);
                    data_index++;
                }
            }

            deleteBuffers(buffers);
            return true;
        }

        bool write(int secNr, const void* data, int secCnt){
            if(m_status != RAID_OK && m_status != RAID_DEGRADED){
                return false;
            }

            if(secNr < 0 || secCnt <= 0 || secNr + secCnt > size() ){
                return false;
            }

            int startingSector, startingDisk, endingSector, endingDisk,row_size;
            calculateIndexes(secNr, secCnt, 
                startingSector, startingDisk, endingSector, endingDisk, row_size
            );

            char** buffers;
            initBuffers(buffers, row_size);

            // reading previous data  fow writing new data and calculate parity.
            if(readDiskData(buffers, startingSector, row_size) == false){
                deleteBuffers(buffers);
                return false;
            }

            fixDatas(buffers,row_size);

            const char* dataPtr = static_cast<const char*>(data);
            for(int i = 0; i < row_size; i++){
                int parity_disk = (startingSector + i) % m_Dev.m_Devices;
                for(int j = 0; j < m_Dev.m_Devices; j++){
                    if( j == parity_disk) continue;
                    if((i == 0 && j < startingDisk) || (i == row_size - 1 && j > endingDisk)) continue;
                    memcpy(buffers[j] + i*SECTOR_SIZE, dataPtr, SECTOR_SIZE);
                    dataPtr += SECTOR_SIZE;
                }

                for (int k = 0; k < SECTOR_SIZE; k++) {
                    char parity = 0;
                    for (int j = 0; j < m_Dev.m_Devices; j++) {
                        if (j == parity_disk) continue;
                        parity ^= buffers[j][i * SECTOR_SIZE + k];
                    }
                    buffers[parity_disk][i * SECTOR_SIZE + k] = parity;
                }
            }

            writeDiskData(buffers, startingSector, row_size);
            deleteBuffers(buffers);
            if(m_status == RAID_FAILED){
                return false;
            }
            return true;
        }

    protected:
        // todo

    void calculateIndexes(
        int secNr, 
        int secCnt, 
        int& startingSector, 
        int& startingDisk, 
        int& endingSector, 
        int& endingDisk,
        int& row_size
        ) {

        startingSector = secNr / (m_Dev.m_Devices - 1) + config_size;
        startingDisk = secNr % (m_Dev.m_Devices - 1);
        int parity_at_start_disk = (startingSector) % m_Dev.m_Devices;
        if(startingDisk >= parity_at_start_disk){
            startingDisk++;
        }
        endingSector = (secNr + secCnt - 1) / (m_Dev.m_Devices - 1) + config_size;
        endingDisk = (secNr + secCnt - 1) % (m_Dev.m_Devices - 1);
        int parity_at_end_disk = (endingSector) % m_Dev.m_Devices;
        if(endingDisk >= parity_at_end_disk){
            endingDisk++;
        }

        row_size = endingSector - startingSector + 1;
    }

    bool readDiskData(char** buffers, int startingSector, int row_size) {
        for(int j = 0; j < m_Dev.m_Devices; j++){
            int ret = m_Dev.m_Read(j, startingSector, buffers[j], row_size);
            if(ret != row_size){
                if(m_status == RAID_DEGRADED){
                    if(degraded_disk != j){
                        m_status = RAID_FAILED;
                        return false;
                    }
                }else if(m_status == RAID_OK){
                    m_status = RAID_DEGRADED;
                    degraded_disk = j;
                }
            }
        }
        return true;
    }

    void writeDiskData(char** buffers, int startingSector, int row_size){
		for(int i = 0; i < m_Dev.m_Devices; i++){
			int ret = m_Dev.m_Write(i, startingSector, buffers[i], row_size);
			if(ret != row_size){
				if(m_status == RAID_DEGRADED){
					if(degraded_disk != i){
						m_status = RAID_FAILED;
					}
				} else if(m_status == RAID_OK) {
					m_status = RAID_DEGRADED;
					degraded_disk = i;
				}
			}
		}
	}

    void fixDatas(char** buffers, int row_size){
        if(m_status == RAID_DEGRADED){ //  degraded_disk available
            char* parity_xor = new char[SECTOR_SIZE];
            for(int i = 0; i < row_size; i++){
                memset(parity_xor, 0, SECTOR_SIZE);
                for(int j = 0; j < m_Dev.m_Devices; j++){
                    if(j == degraded_disk) continue;
                    for(int k = 0; k < SECTOR_SIZE; k++){
                        parity_xor[k] ^= buffers[j][i* SECTOR_SIZE + k];
                    }
                }
                memcpy(buffers[degraded_disk] + i*SECTOR_SIZE, parity_xor, SECTOR_SIZE);
            }
            delete [] parity_xor;
        }
    }

    void initBuffers(char**& buffers, int row_size){
        buffers = new char*[m_Dev.m_Devices];
        for (int i = 0; i < m_Dev.m_Devices; i++){
            buffers[i] = new char[SECTOR_SIZE * row_size];
        }
    }

    void deleteBuffers(char**& buffers){
        for (int i = 0; i < m_Dev.m_Devices; i++){
            delete[] buffers[i];
        }
        delete[] buffers;
    }
        



};

#ifndef __PROGTEST__
// #include "tests.inc"


constexpr int                          RAID_DEVICES = 4;
constexpr int                          DISK_SECTORS = 8192;
static FILE                          * g_Fp[RAID_DEVICES];

//-------------------------------------------------------------------------------------------------
/** Sample sector reading function. The function will be called by your Raid driver implementation.
 * Notice, the function is not called directly. Instead, the function will be invoked indirectly
 * through function pointer in the TBlkDev structure.
 */
int diskRead ( int device, int  sectorNr, void* data, int  sectorCnt )
{
    if ( device < 0 || device >= RAID_DEVICES )
        return 0;
    if ( g_Fp[device] == nullptr )
        return 0;
    if ( sectorCnt <= 0 || sectorNr + sectorCnt > DISK_SECTORS )
        return 0;
    fseek ( g_Fp[device], sectorNr * SECTOR_SIZE, SEEK_SET );
    return fread ( data, SECTOR_SIZE, sectorCnt, g_Fp[device] );
}
//-------------------------------------------------------------------------------------------------
/** Sample sector writing function. Similar to diskRead
 */
int diskWrite  ( int device, int sectorNr,const void  * data, int sectorCnt )
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
            g_Fp[i]  = nullptr;
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
            throw std::runtime_error ( "Raw storage create error" );
        }

        for ( int j = 0; j < DISK_SECTORS; j ++ )
            if ( fwrite ( buffer, sizeof ( buffer ), 1, g_Fp[i] ) != 1 )
            {
                doneDisks ();
                throw std::runtime_error ( "Raw storage create error" );
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
            throw std::runtime_error ( "Raw storage access error" );
        }
        fseek ( g_Fp[i], 0, SEEK_END );
        if ( ftell ( g_Fp[i] ) != DISK_SECTORS * SECTOR_SIZE )
        {
            doneDisks ();
            throw std::runtime_error ( "Raw storage read error" );
        }
    }
    res . m_Devices = RAID_DEVICES;
    res . m_Sectors = DISK_SECTORS;
    res . m_Read    = diskRead;
    res . m_Write   = diskWrite;
    return res;
}

void degradeDisk(int diskIndex, bool deleteFile = true)
{
    if (diskIndex < 0 || diskIndex >= RAID_DEVICES) {
        throw std::runtime_error("Disk index out of range");
    }

    if (g_Fp[diskIndex] == nullptr) {
        throw std::runtime_error("Disk already degraded or not initialized");
    }
    fclose(g_Fp[diskIndex]);
    g_Fp[diskIndex] = nullptr;

    if (deleteFile) {
        char fn[100];
        snprintf(fn, sizeof(fn), "/tmp/%04d", diskIndex);
        if (std::remove(fn) != 0) {
            throw std::runtime_error("Failed to delete disk file");
        }
    }
}

void createandputDisk(int diskIndex){
    char       buffer[SECTOR_SIZE];
    memset    ( buffer, 0, sizeof ( buffer ) );

    char       fn[100];
    if (diskIndex < 0 || diskIndex >= RAID_DEVICES) {
        throw std::runtime_error("Disk index out of range");
    }

    if (g_Fp[diskIndex] != nullptr) {
        throw std::runtime_error("Disk already exist");
    }

    snprintf( fn, sizeof ( fn ), "/tmp/%04d", diskIndex );
    g_Fp[diskIndex] = fopen(fn, "w+b");
    if (!g_Fp[diskIndex]){
        doneDisks();
        throw std::runtime_error ( "Raw storage create error" );
    }

    for ( int j = 0; j < DISK_SECTORS; j ++ )
    if ( fwrite ( buffer, sizeof ( buffer ), 1, g_Fp[diskIndex] ) != 1 )
    {
        doneDisks ();
        throw std::runtime_error ( "Raw storage create error" );
    }

}
//-------------------------------------------------------------------------------------------------
void test0 (){
    printf("initialization and finalization test\n");
    TBlkDev  dev = createDisks ();
    assert ( CRaidVolume::create ( dev ) );
    CRaidVolume vol;
    assert ( vol . start ( dev ) == RAID_OK );
    assert ( vol . status () == RAID_OK );
    assert ( vol . stop () == RAID_STOPPED );
    assert ( vol . status () == RAID_STOPPED );
    doneDisks ();
}
void test_read (){
    printf("read one time\n");
    TBlkDev  dev = createDisks ();
    assert ( CRaidVolume::create ( dev ) );
    CRaidVolume vol;
    assert ( vol . start ( dev ) == RAID_OK );
    assert ( vol . status () == RAID_OK );
    char buffer [SECTOR_SIZE];
    assert ( vol . read ( 0, buffer, 1 ) );

    assert ( vol . stop () == RAID_STOPPED );
    assert ( vol . status () == RAID_STOPPED );
    doneDisks ();
}

void test_read_error (){
    printf("read error \n");
    TBlkDev  dev = createDisks ();
    assert ( CRaidVolume::create ( dev ) );
    CRaidVolume vol;
    assert ( vol . start ( dev ) == RAID_OK );
    assert ( vol . status () == RAID_OK );
    char* buffer = new char[SECTOR_SIZE + vol.size()+10];
    assert ( vol . status () == RAID_OK );
    assert (!vol . read ( 0, buffer, vol.size()+1));
    assert (!vol . read ( 5, buffer, vol.size()+5));
    assert ( vol . status () == RAID_OK );

    delete [] buffer;

    assert ( vol . stop () == RAID_STOPPED );
    assert ( vol . status () == RAID_STOPPED );
    doneDisks ();
}

void test1(){
    printf("read value is zero\n");
    TBlkDev  dev = createDisks ();
    assert ( CRaidVolume::create ( dev ) );
    CRaidVolume vol;
    assert ( vol . start ( dev ) == RAID_OK );
    assert ( vol . status () == RAID_OK );
    for ( int i = 0; i < vol.size(); i++ ){
        char buffer [SECTOR_SIZE];
        char reference_buffer[SECTOR_SIZE];
        assert ( vol . read ( i, buffer, 1 ) );
        memcpy(reference_buffer, buffer, SECTOR_SIZE);
        assert(memcmp(buffer, reference_buffer, SECTOR_SIZE) == 0); 
    }
    assert ( vol . stop () == RAID_STOPPED );
    assert ( vol . status () == RAID_STOPPED );
    doneDisks ();
}

void                                   test2                                ()
{
    printf("content correctness 1 sector\n");
    TBlkDev  dev = createDisks ();
    assert ( CRaidVolume::create ( dev ) );
    CRaidVolume vol;
    assert ( vol . start ( dev ) == RAID_OK );
    assert ( vol . status () == RAID_OK );
    int size = 1;
    for( int i = 0; i < vol.size(); i++){
        // printf("%d\n", i);
        assert(vol.status() == RAID_OK);
        char buffer[SECTOR_SIZE*size];
        char reference_buffer[SECTOR_SIZE*size];

        for (int j = 0; j < SECTOR_SIZE*size; j++) {
            buffer[j] = rand() % 256;
        }

        memcpy(reference_buffer, buffer, SECTOR_SIZE*size);
        assert(vol.write( i, buffer, size));
        assert(vol.status() == RAID_OK);

        memset(buffer, 0, SECTOR_SIZE*size);
        assert(vol.read( i, buffer, size));
        assert(vol.status() == RAID_OK);
        assert(memcmp(buffer, reference_buffer, SECTOR_SIZE*size) == 0);
    }
    assert ( vol . stop () == RAID_STOPPED );
    assert ( vol . status () == RAID_STOPPED );
    doneDisks ();
}

void test3 ()
{
    printf("content correctness n sector\n");
    TBlkDev  dev = createDisks ();
    assert ( CRaidVolume::create ( dev ) );
    CRaidVolume vol;
    assert ( vol . start ( dev ) == RAID_OK );
    assert ( vol . status () == RAID_OK );
    for(int size = 2; size < 1000; size *= size ){
        // printf("%d\n", size);
        for( int i = 0; i < 200 - (size-1); i++){
            assert(vol.status() == RAID_OK);
            char buffer[SECTOR_SIZE*size];
            char reference_buffer[SECTOR_SIZE*size];

            for (int j = 0; j < SECTOR_SIZE*size; j++) {
                buffer[j] = rand() % 256;
            }

            memcpy(reference_buffer, buffer, SECTOR_SIZE*size);
            assert(vol.write( i, buffer, size));
            assert(vol.status() == RAID_OK);
            memset(buffer, 0, SECTOR_SIZE*size);
            assert(vol.read( i, buffer, size));
            assert(vol.status() == RAID_OK);
            assert(memcmp(buffer, reference_buffer, SECTOR_SIZE*size) == 0);
        }

    }
    
    assert ( vol . stop () == RAID_STOPPED );
    assert ( vol . status () == RAID_STOPPED );
    doneDisks ();
}




void  test_multi_write ()
{
    printf("multi read write \n");
    TBlkDev  dev = createDisks ();
    assert ( CRaidVolume::create ( dev ) );
    CRaidVolume vol;
    assert ( vol . start ( dev ) == RAID_OK );
    assert ( vol . status () == RAID_OK );
    int size = 1;
    for( int i = 0; i < 1; i++){
        assert(vol.status() == RAID_OK);
        char buffer[SECTOR_SIZE*size];
        char buffer2[SECTOR_SIZE*size];
        char reference_buffer[SECTOR_SIZE*size];
        char reference_buffer2[SECTOR_SIZE*size];

        for (int j = 0; j < SECTOR_SIZE*size; j++) {
            buffer[j] = rand() % 256;
            buffer2[j] = rand() % 256;
        }

        memcpy(reference_buffer, buffer, SECTOR_SIZE*size);
        memcpy(reference_buffer2, buffer2, SECTOR_SIZE*size);


        assert(vol.write( i, buffer, size));
        assert(vol.status() == RAID_OK);

    
        assert(vol.write(i+size, buffer2, size));
        assert(vol.status() == RAID_OK);

        memset(buffer, 0, SECTOR_SIZE*size);
        assert(vol.read( i, buffer, size));
        assert(vol.status() == RAID_OK);

        memset(buffer2, 0, SECTOR_SIZE*size);
        assert(vol.read(i+size, buffer2, size));
        assert(vol.status() == RAID_OK);



        assert(memcmp(buffer, reference_buffer, SECTOR_SIZE*size) == 0);
        // print content of buffer and reference_buffer in hex
        
        
        assert(memcmp(buffer2, reference_buffer2, SECTOR_SIZE*size) == 0);
    }
    assert ( vol . stop () == RAID_STOPPED );
    assert ( vol . status () == RAID_STOPPED );
    doneDisks ();
}


void test4 ()
{
    printf("test4 read degrade\n");
    TBlkDev  dev = createDisks ();
    assert ( CRaidVolume::create ( dev ) );
    CRaidVolume vol;
    assert ( vol . start ( dev ) == RAID_OK );
    assert ( vol . status () == RAID_OK );
    int size = 8;
   
    assert(vol.status() == RAID_OK);
    char buffer[SECTOR_SIZE*size];
    char reference_buffer[SECTOR_SIZE*size];
    for (int j = 0; j < SECTOR_SIZE*size; j++) {
        buffer[j] = rand() % 256;
    }
    memcpy(reference_buffer, buffer, SECTOR_SIZE*size);
    assert(vol.write( 1, buffer, size));
    assert(vol.status() == RAID_OK);
    memset(buffer, 0, SECTOR_SIZE*size);
    assert(vol.read( 1, buffer, size));
    assert(vol.status() == RAID_OK);

    assert(memcmp(buffer, reference_buffer, SECTOR_SIZE*size) == 0);
    degradeDisk(2);
    assert(vol.read( 1, buffer, size));
    assert(vol.status() == RAID_DEGRADED);
    //read failed here.
    assert(memcmp(buffer, reference_buffer, SECTOR_SIZE*size) == 0);
    assert(vol.status() == RAID_DEGRADED);

    assert( vol . stop () == RAID_STOPPED );
    assert( vol . status () == RAID_STOPPED );
    doneDisks ();
}


void test5(){
    printf("write degrade\n");
    TBlkDev  dev = createDisks ();
    assert ( CRaidVolume::create ( dev ) );
    CRaidVolume vol;
    assert ( vol . start ( dev ) == RAID_OK );
    assert ( vol . status () == RAID_OK );
    int size = 8;
   
    
    char buffer[SECTOR_SIZE*size];
    char reference_buffer[SECTOR_SIZE*size];
    for (int j = 0; j < SECTOR_SIZE*size; j++) {
        buffer[j] = rand() % 256;
    }

    assert(vol.status() == RAID_OK);
    memcpy(reference_buffer, buffer, SECTOR_SIZE*size);
    degradeDisk(2);
    assert(vol.write( 1, buffer, size));
    assert(vol.status() == RAID_DEGRADED);
    memset(buffer, 0, SECTOR_SIZE*size);
    assert(vol.read( 1, buffer, size));
    assert(vol.status() == RAID_DEGRADED);
    assert(memcmp(buffer, reference_buffer, SECTOR_SIZE*size) == 0);
    

    assert( vol . stop () == RAID_STOPPED );
    assert( vol . status () == RAID_STOPPED );
    doneDisks ();
}


void  test_resync ()
{
    printf("est_resync \n");
    TBlkDev  dev = createDisks ();
    assert ( CRaidVolume::create ( dev ) );
    CRaidVolume vol;
    assert ( vol . start ( dev ) == RAID_OK );
    assert ( vol . status () == RAID_OK );
    int size = 1;
    for( int i = 0; i < 20; i++, size *=size){
        assert(vol.status() == RAID_OK);
        char buffer[SECTOR_SIZE*size];
        char buffer2[SECTOR_SIZE*size];
        char reference_buffer[SECTOR_SIZE*size];
        char reference_buffer2[SECTOR_SIZE*size];

        for (int j = 0; j < SECTOR_SIZE*size; j++) {
            buffer[j] = rand() % 256;
            buffer2[j] = rand() % 256;
        }
        memcpy(reference_buffer, buffer, SECTOR_SIZE*size);
        memcpy(reference_buffer2, buffer2, SECTOR_SIZE*size);

        assert(vol.write( i, buffer, size));
        assert(vol.status() == RAID_OK);
    
        assert(vol.write(i+size, buffer2, size));
        assert(vol.status() == RAID_OK);

        
        degradeDisk(2);
        memset(buffer, 0, SECTOR_SIZE*size);
        assert(vol.read( i, buffer, size));
        assert(vol.status() == RAID_DEGRADED);

        memset(buffer2, 0, SECTOR_SIZE*size);
        assert(vol.read(i+size, buffer2, size));
        assert(vol.status() == RAID_DEGRADED);


        assert(memcmp(buffer, reference_buffer, SECTOR_SIZE*size) == 0);
        assert(memcmp(buffer2, reference_buffer2, SECTOR_SIZE*size) == 0);

        createandputDisk(2);
        int ret = vol.resync();
        // printf("%d\n", ret);
        assert(ret == RAID_OK);

        memset(buffer, 0, SECTOR_SIZE*size);
        assert(vol.read( i, buffer, size));
        assert(vol.status() == RAID_OK);
        memset(buffer2, 0, SECTOR_SIZE*size);
        assert(vol.read(i+size, buffer2, size));
        assert(vol.status() == RAID_OK);


        assert(memcmp(buffer, reference_buffer, SECTOR_SIZE*size) == 0);
        assert(memcmp(buffer2, reference_buffer2, SECTOR_SIZE*size) == 0);

    }
    assert ( vol . stop () == RAID_STOPPED );
    assert ( vol . status () == RAID_STOPPED );
    doneDisks ();
}


void  test_resync_whole ()
{
    printf("test_resync_whole \n");
    TBlkDev  dev = createDisks ();
    assert ( CRaidVolume::create ( dev ) );
    CRaidVolume vol;
    assert ( vol . start ( dev ) == RAID_OK );
    assert ( vol . status () == RAID_OK );
    int size = 100;
    // printf("%d\n", size);

    assert(vol.status() == RAID_OK);
    char buffer[SECTOR_SIZE*size];
    char reference_buffer[SECTOR_SIZE*size];
   

    for (int j = 0; j < SECTOR_SIZE*size; j++) {
        buffer[j] = rand() % 256;
    }
    memcpy(reference_buffer, buffer, SECTOR_SIZE*size);

    assert(vol.write( vol.size()-size, buffer, size));
    assert(vol.status() == RAID_OK);

    memset(buffer, 0, SECTOR_SIZE*size);

    degradeDisk(3);
    assert(vol.read(vol.size()-size, buffer, size));
    assert(vol.status() == RAID_DEGRADED);
   

    assert(memcmp(buffer, reference_buffer, SECTOR_SIZE*size) == 0);

    createandputDisk(3);

    int ret = vol.resync();
    // printf("%d\n", ret);

    assert(ret == RAID_OK);
    memset(buffer, 0, SECTOR_SIZE*size);

    assert(vol.read(vol.size()-size, buffer, size));
    assert(vol.status() == RAID_OK);

    assert(memcmp(buffer, reference_buffer, SECTOR_SIZE*size) == 0);

    assert ( vol . stop () == RAID_STOPPED );
    assert ( vol . status () == RAID_STOPPED );
    doneDisks ();
}


void  test_offline_replace()
{
    printf("test_offline_replace \n");
    TBlkDev  dev = createDisks ();
    assert ( CRaidVolume::create ( dev ) );
    CRaidVolume vol;
    assert ( vol . start ( dev ) == RAID_OK );
    assert ( vol . status () == RAID_OK );
    int size = 100;
    // printf("%d\n", size);

    assert(vol.status() == RAID_OK);
    char buffer[SECTOR_SIZE*size];
    char reference_buffer[SECTOR_SIZE*size];
   

    for (int j = 0; j < SECTOR_SIZE*size; j++) {
        buffer[j] = rand() % 256;
    }
    memcpy(reference_buffer, buffer, SECTOR_SIZE*size);

    assert(vol.write( vol.size()-size, buffer, size));
    assert(vol.status() == RAID_OK);

    memset(buffer, 0, SECTOR_SIZE*size);

    degradeDisk(3);
    assert(vol.read(vol.size()-size, buffer, size));
    assert(vol.status() == RAID_DEGRADED);
    assert(memcmp(buffer, reference_buffer, SECTOR_SIZE*size) == 0);

    assert ( vol . stop () == RAID_STOPPED );
    assert ( vol . status () == RAID_STOPPED );

    CRaidVolume vol2;

    assert ( vol2 . start ( dev ) == RAID_DEGRADED);
    assert ( vol2 . status () == RAID_DEGRADED);

    createandputDisk(3);

    int ret = vol2.resync();
    printf("%d\n", ret);

    assert(ret == RAID_OK);
    memset(buffer, 0, SECTOR_SIZE*size);

    assert(vol2.read(vol2.size()-size, buffer, size));
    assert(vol2.status() == RAID_OK);

    assert(memcmp(buffer, reference_buffer, SECTOR_SIZE*size) == 0);

    assert ( vol2. stop () == RAID_STOPPED );
    assert ( vol2 . status () == RAID_STOPPED );
    doneDisks ();
}



//-------------------------------------------------------------------------------------------------
void                                   test20                                  ()
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
    test0();
    test_read();
    test_read_error();
    test1 ();
    test2 ();
    test_multi_write();
    test3();
    test4();
    test5();
    test_resync();
    test_resync_whole();
    test_offline_replace();
    return EXIT_SUCCESS;
}






#endif /* __PROGTEST__ */
