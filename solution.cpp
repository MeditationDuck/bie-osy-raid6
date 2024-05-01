#ifndef __PROGTEST__

#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cassert>
#include <stdexcept>
#include <iostream>
#include <iomanip>
using namespace std;

constexpr int                          SECTOR_SIZE                             =              32;
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
	static constexpr unsigned char gf_inv[256] = {
		0x00, 0x01, 0x8d, 0xf6, 0xcb, 0x52, 0x7b, 0xd1, 0xe8, 0x4f, 0x29, 0xc0, 0xb0, 0xe1, 0xe5, 0xc7,
		0x74, 0xb4, 0xaa, 0x4b, 0x99, 0x2b, 0x60, 0x5f, 0x58, 0x3f, 0xfd, 0xcc, 0xff, 0x40, 0xee, 0xb2,
		0x3a, 0x6e, 0x5a, 0xf1, 0x55, 0x4d, 0xa8, 0xc9, 0xc1, 0x0a, 0x98, 0x15, 0x30, 0x44, 0xa2, 0xc2,
		0x2c, 0x45, 0x92, 0x6c, 0xf3, 0x39, 0x66, 0x42, 0xf2, 0x35, 0x20, 0x6f, 0x77, 0xbb, 0x59, 0x19,
		0x1d, 0xfe, 0x37, 0x67, 0x2d, 0x31, 0xf5, 0x69, 0xa7, 0x64, 0xab, 0x13, 0x54, 0x25, 0xe9, 0x09,
		0xed, 0x5c, 0x05, 0xca, 0x4c, 0x24, 0x87, 0xbf, 0x18, 0x3e, 0x22, 0xf0, 0x51, 0xec, 0x61, 0x17,
		0x16, 0x5e, 0xaf, 0xd3, 0x49, 0xa6, 0x36, 0x43, 0xf4, 0x47, 0x91, 0xdf, 0x33, 0x93, 0x21, 0x3b,
		0x79, 0xb7, 0x97, 0x85, 0x10, 0xb5, 0xba, 0x3c, 0xb6, 0x70, 0xd0, 0x06, 0xa1, 0xfa, 0x81, 0x82,
		0x83, 0x7e, 0x7f, 0x80, 0x96, 0x73, 0xbe, 0x56, 0x9b, 0x9e, 0x95, 0xd9, 0xf7, 0x02, 0xb9, 0xa4,
		0xde, 0x6a, 0x32, 0x6d, 0xd8, 0x8a, 0x84, 0x72, 0x2a, 0x14, 0x9f, 0x88, 0xf9, 0xdc, 0x89, 0x9a,
		0xfb, 0x7c, 0x2e, 0xc3, 0x8f, 0xb8, 0x65, 0x48, 0x26, 0xc8, 0x12, 0x4a, 0xce, 0xe7, 0xd2, 0x62,
		0x0c, 0xe0, 0x1f, 0xef, 0x11, 0x75, 0x78, 0x71, 0xa5, 0x8e, 0x76, 0x3d, 0xbd, 0xbc, 0x86, 0x57,
		0x0b, 0x28, 0x2f, 0xa3, 0xda, 0xd4, 0xe4, 0x0f, 0xa9, 0x27, 0x53, 0x04, 0x1b, 0xfc, 0xac, 0xe6,
		0x7a, 0x07, 0xae, 0x63, 0xc5, 0xdb, 0xe2, 0xea, 0x94, 0x8b, 0xc4, 0xd5, 0x9d, 0xf8, 0x90, 0x6b,
		0xb1, 0x0d, 0xd6, 0xeb, 0xc6, 0x0e, 0xcf, 0xad, 0x08, 0x4e, 0xd7, 0xe3, 0x5d, 0x50, 0x1e, 0xb3,
		0x5b, 0x23, 0x38, 0x34, 0x68, 0x46, 0x03, 0x8c, 0xdd, 0x9c, 0x7d, 0xa0, 0xcd, 0x1a, 0x41, 0x1c
	};
	

	public:
		TBlkDev m_Dev;
		int m_status = RAID_STOPPED;
		int config_size = 1;// sector
		int degraded_disk = -1;
		int degraded_disk2 = -1;
		struct DiskState {
			int touched;
			int disk_index;
			int m_status;
			int degraded_disk;
			int degraded_disk2;
			DiskState(int i):touched(-1),disk_index(i),m_status(RAID_OK),degraded_disk(-1), degraded_disk2(-1){}
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
			if(m_status == RAID_STOPPED){
                return m_status;
            }
			if(m_status == RAID_OK || m_status == RAID_DEGRADED){
                char* data = new char[SECTOR_SIZE];
                for(int i = 0; i < m_Dev.m_Devices;i++){
                    // if(m_status == RAID_DEGRADED && i == degraded_disk) continue;
					// if(m_status == RAID_DEGRADED && i == degraded_disk2) continue;
                    DiskState state(i);
                    state.degraded_disk = degraded_disk;
					state.degraded_disk2 = degraded_disk2;
                    state.m_status = m_status;
                    memset(data, 0, SECTOR_SIZE);
                    memcpy(data, &state, sizeof(DiskState));
                    int ret = m_Dev.m_Write(i, 0, data, 1);
                    if(ret != 1 && m_status == RAID_DEGRADED ){
						if(i != degraded_disk && i  !=  degraded_disk2){
							m_status = RAID_FAILED;
						}
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

		int resync (){
			if(m_status != RAID_DEGRADED){
                return m_status;
            }
			int granurity = 16;
            char** buffers;
            int sector_curr = config_size;
            int size_of_drive = m_Dev.m_Sectors;

			initBuffers(buffers, granurity);

			while(sector_curr < size_of_drive){
				if(size_of_drive - sector_curr < granurity){
					granurity = size_of_drive - sector_curr;
				}
				if(readDiskData(buffers, sector_curr, granurity)== false){
					deleteBuffers(buffers);
					return m_status;
				}
				fixDatas(buffers, sector_curr ,granurity);
				writeDiskData(buffers, sector_curr, granurity);
				sector_curr += granurity;
			}

			deleteBuffers(buffers);
            m_status = RAID_OK;
            return m_status;
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

			int startingSector, startingDisk, endingSector, endingDisk,row_size;
            calcIndexes(secNr, secCnt, 
                startingSector, startingDisk, endingSector, endingDisk, row_size
            );
			char** buffers;
			initBuffers(buffers, row_size);
			if(readDiskData(buffers, startingSector, row_size) == false){
                deleteBuffers(buffers);
				// printf("failllll\n");
                return false;
			}

			// printf("status: %d,disks:  %d, %d\n", m_status, degraded_disk, degraded_disk2);

			fixDatas(buffers, startingSector, row_size);

			int data_index = 0;
            char* dataPtr = static_cast<char*>(data);
            for(int i = 0; i < row_size; i++){
				int parity_p = (startingSector + i) % m_Dev.m_Devices;
				int parity_q = (startingSector + i + 1) % m_Dev.m_Devices;
                for(int j = 0; j < m_Dev.m_Devices; j++){
                    if(j == parity_p || j == parity_q) continue;
                    if((i == 0 && j < startingDisk) || (i == row_size - 1 && j > endingDisk)) continue;
                    memcpy(dataPtr + (data_index * SECTOR_SIZE), buffers[j] + (i * SECTOR_SIZE), SECTOR_SIZE);
                    data_index++;
                }
			}
            deleteBuffers(buffers);
            return true;
		}


		bool write (int secNr, const void* data, int secCnt){
			if(m_status != RAID_OK && m_status != RAID_DEGRADED){
				return false;
			}
			if(secNr < 0 || secCnt <= 0 || secNr + secCnt > size() ){
				return false;
			}

			int startingSector, startingDisk, endingSector, endingDisk,row_size;
            calcIndexes(secNr, secCnt, 
                startingSector, startingDisk, endingSector, endingDisk, row_size
            );

			char** buffers;
			initBuffers(buffers, row_size);
			if(readDiskData(buffers, startingSector, row_size) == false){
                deleteBuffers(buffers);
                return false;
			}
			fixDatas(buffers, startingSector, row_size);

			// create parity buffers 
			const char* dataPtr = static_cast<const char*>(data);
            for(int i = 0; i < row_size; i++){
                int parity_p = (startingSector + i) % m_Dev.m_Devices;
				int parity_q = (startingSector + i+1) % m_Dev.m_Devices;
				// just copy data
                for(int j = 0; j < m_Dev.m_Devices; j++){
					if( j == parity_p || j == parity_q){
						continue;
					}
                    if((i == 0 && j < startingDisk) || (i == row_size - 1 && j > endingDisk)){
						continue;
					}
                    memcpy(buffers[j] + i * SECTOR_SIZE, dataPtr, SECTOR_SIZE);
                    dataPtr += SECTOR_SIZE;
                }
				// create parity data
                for (int k = 0; k < SECTOR_SIZE; k++) {
                    char parity = 0;
                    for (int j = 0; j < m_Dev.m_Devices; j++) {
                        if (j == parity_p || j == parity_q) continue;
                        parity ^= buffers[j][i * SECTOR_SIZE + k];
                    }
                    buffers[parity_p][i * SECTOR_SIZE + k] = parity;
                }

				for(int k = 0; k < SECTOR_SIZE; k++){
					char parity = 0;
					int data_index = 0;
                    for (int j = 0; j < m_Dev.m_Devices; j++) {
                        if (j == parity_p || j == parity_q) continue;
						unsigned char cx = 1 << (data_index%8);
                        parity ^= mul(cx, buffers[j][i * SECTOR_SIZE + k]);
						data_index ++;
                    }
                    buffers[parity_q][i * SECTOR_SIZE + k] = parity;
				}
            }

			// write to disks.
            writeDiskData(buffers, startingSector, row_size);
			deleteBuffers(buffers); // I guess without return value check it work.
			if(m_status == RAID_FAILED){
                return false;
            }
			return true;
		}
  	protected:
    // todo

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
	void calcIndexes(
		int secNr, 
        int secCnt, 
        int& startingSector, 
        int& startingDisk, 
        int& endingSector, 
        int& endingDisk,
        int& row_size
	){

		startingSector = secNr / (m_Dev.m_Devices - 2) + config_size;
		startingDisk = secNr % (m_Dev.m_Devices - 2);
		int	parity_at_start_disk_p = (startingSector) % m_Dev.m_Devices;
		int parity_at_start_disk_q = (startingSector + 1) % m_Dev.m_Devices;
		if(startingDisk >= parity_at_start_disk_p){
			startingDisk++;
		}
		if(startingDisk >= parity_at_start_disk_q){
			startingDisk++;
		}

		endingSector = (secNr+secCnt-1) / (m_Dev.m_Devices - 2) + config_size;
		endingDisk = (secNr+secCnt-1) % (m_Dev.m_Devices - 2);
		int parity_at_end_disk_p = (endingSector) % m_Dev.m_Devices;
		int parity_at_end_disk_q = (endingSector+1) % m_Dev.m_Devices;
		if(endingDisk >= parity_at_end_disk_p){
			endingDisk++;
		}
		if(endingDisk >= parity_at_end_disk_q){
			endingDisk++;
		}
		row_size = endingSector - startingSector + 1;
	}

    bool readDiskData(char** buffers, int startingSector, int row_size) {
		for(int j = 0; j < m_Dev.m_Devices; j++){
			int ret = m_Dev.m_Read(j, startingSector, buffers[j], row_size);
			if(ret != row_size){
				// printf("read differnet!!\n");
				if(m_status == RAID_DEGRADED){
					if(degraded_disk != j && degraded_disk2 != -1 && degraded_disk2 !=j){
						m_status = RAID_FAILED;
						return false;
					}else if(degraded_disk != j && degraded_disk2 == -1){
						// m_status does not change.
						degraded_disk2 = j;
					}
				}else if(m_status == RAID_OK){
					m_status = RAID_DEGRADED;
					degraded_disk = j;
				}
			}
		}
		return true;
	}

	bool writeDiskData(char** buffers, int startingSector, int row_size){
		for(int i = 0; i < m_Dev.m_Devices; i++){
			int ret = m_Dev.m_Write(i, startingSector, buffers[i], row_size);
			if(ret != row_size){
				if(m_status == RAID_DEGRADED){
					if(degraded_disk != i && degraded_disk2 != -1 && degraded_disk2 !=i){
						m_status = RAID_FAILED;
						return false;
					}else if(degraded_disk != i && degraded_disk2 == -1){
						// m_status does not change.
						degraded_disk2 = i;
					}
				}else if(m_status == RAID_OK){
					m_status = RAID_DEGRADED;
					degraded_disk = i;
				}
			}
		}
		return true;
	}

	void fixDatas(char** buffers,  int startingSector, int row_size){
		if(m_status == RAID_DEGRADED){ //  degraded_disk available
			char* parity_xor = new char[SECTOR_SIZE];
			if(degraded_disk2 == -1){
				// printf("mut be hehe %d %d\n", startingSector, row_size);
				for(int i = 0; i < row_size; i++){
					int parity_p = (startingSector + i) % m_Dev.m_Devices;
					int parity_q = (startingSector + i + 1) % m_Dev.m_Devices;
					// printf("pq: %d, %d\n", parity_p, parity_q);
					if(degraded_disk == parity_p || degraded_disk == parity_q){
						// about this row degraded disk is parity, data does not effected.
						// printf("expected two line\n");
						continue;
					}
					memset(parity_xor, 0, SECTOR_SIZE);
					for(int j = 0; j < m_Dev.m_Devices; j++){
						if(j == degraded_disk || j == parity_q) continue;
						for(int k = 0; k < SECTOR_SIZE; k++){
							parity_xor[k] ^= buffers[j][i* SECTOR_SIZE + k];
						}
					}
					memcpy(buffers[degraded_disk] + i*SECTOR_SIZE, parity_xor, SECTOR_SIZE);
				}
			}else{
				// two disk are degraded.
				for(int i = 0; i < row_size; i++){
					// fnd parity p and q at this row.
					int parity_p = (startingSector + i) % m_Dev.m_Devices;
					int parity_q = (startingSector + i + 1) % m_Dev.m_Devices;
					if(
						(degraded_disk == parity_p && degraded_disk2 == parity_q) ||
						(degraded_disk == parity_q && degraded_disk2 == parity_p)
					){
						// if it is only parity then data are safe!!
						continue;
					}else if(degraded_disk == parity_q || degraded_disk2 == parity_q){
						// in this case one of data are deprecated, this is same as raid5
						int degraded_data_disk = degraded_disk == parity_q ? degraded_disk2 : degraded_disk;
						memset(parity_xor, 0, SECTOR_SIZE);
						for(int j = 0; j < m_Dev.m_Devices; j++){
							if(j == degraded_data_disk || j == parity_q) continue;
							for(int k = 0; k < SECTOR_SIZE; k++){
								parity_xor[k] ^= buffers[j][i* SECTOR_SIZE + k];
							}
						}
						memcpy(buffers[degraded_data_disk] + i*SECTOR_SIZE, parity_xor, SECTOR_SIZE);
						continue;
					}else if(degraded_disk == parity_p || degraded_disk2 == parity_p){
						// we can calculate this disk, from other data and parity_q
						int degraded_data_disk = degraded_disk == parity_p ? degraded_disk2 : degraded_disk;
						memset(parity_xor, 0, SECTOR_SIZE);
						int data_index = 0;
						for(int j = 0; j < m_Dev.m_Devices; j++){
							if(j == degraded_data_disk){
								data_index++;
								continue;
							}
							if(j == parity_p){ // because this data is degraded
								continue;
							}
							if(j == parity_q){
								for(int k = 0; k < SECTOR_SIZE; k++){
									parity_xor[k] ^= buffers[j][i* SECTOR_SIZE + k];
								}
								continue;
							}
							unsigned char cx = 1 << (data_index%8);
							for(int k = 0; k < SECTOR_SIZE; k++){
								parity_xor[k] ^= mul(cx, buffers[j][i* SECTOR_SIZE + k]);
							}
							data_index++;
						}

						for(int k = 0; k < SECTOR_SIZE; k++){
							parity_xor[k] = mul(gf_inv[1<<degraded_data_disk%8], parity_xor[k]);
						}

						memcpy(buffers[degraded_data_disk] + i*SECTOR_SIZE, parity_xor, SECTOR_SIZE);
						continue;
					}else{
						// must be two data-disks are deprecated.
						memset(parity_xor, 0, SECTOR_SIZE);
						int data_index = 0;
						unsigned char cx = 1 << (degraded_disk%8);
						unsigned char cy = 1 << (degraded_disk2%8);
						for(int j = 0; j < m_Dev.m_Devices; j++){
							if(j == degraded_disk || j == degraded_disk2){
								data_index++;
								continue;
							}
							if(j == parity_q){
								for(int k = 0; k < SECTOR_SIZE; k++){
									parity_xor[k] ^= buffers[j][i* SECTOR_SIZE + k];
								}
								continue;
							}
							for(int k = 0; k < SECTOR_SIZE; k++){
								parity_xor[k] ^= mul(cy,  buffers[j][i* SECTOR_SIZE + k]);
							}
						}

						char val = gf_inv[cx^cy];
						for(int k = 0; k < SECTOR_SIZE; k++){
							parity_xor[k] = mul(val, parity_xor[k]);
						}
						memcpy(buffers[degraded_disk] + i*SECTOR_SIZE, parity_xor, SECTOR_SIZE);

						// so only one disk are failed. so we can just use xor. 
						// this code must be same. it can be somehow shorter.
						memset(parity_xor, 0, SECTOR_SIZE);
						for(int j = 0; j< m_Dev.m_Devices;j ++){
							if(j == degraded_disk2 || j == parity_q) continue;
							for(int k = 0; k < SECTOR_SIZE; k++){
								parity_xor[k] ^= buffers[j][i* SECTOR_SIZE + k];
							}
						}
						memcpy(buffers[degraded_disk2] + i*SECTOR_SIZE, parity_xor, SECTOR_SIZE);
					}
				}
			}
			delete [] parity_xor;
		}
	}

	unsigned char mul(unsigned char a, unsigned char b) {
		unsigned char p = 0;
		for (int i = 0; i < 8; i++) {
			if (b & 1) {
				p ^= a;
			}
			bool carry = (a & 0x80) != 0;
			a <<= 1;

			if (carry) {
				a ^= 0x1B;
			}
			b >>= 1;
		}
		return p;
	}
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
	printf("/tmp/%04d\n", i );
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
// void                                   test1                                   ()
// {
//   /* create the disks before we use them
//    */
//   TBlkDev  dev = createDisks ();
//   /* The disks are ready at this moment. Your RAID-related functions may be executed,
//    * the disk backend is ready.
//    *
//    * First, try to create the RAID:
//    */

//   assert ( CRaidVolume::create ( dev ) );


//   /* start RAID volume */

//   CRaidVolume vol;

//   assert ( vol . start ( dev ) == RAID_OK );
//   assert ( vol . status () == RAID_OK );

//   /* your raid device shall be up.
//    * try to read and write all RAID sectors:
//    */

//   for ( int i = 0; i < vol . size (); i ++ )
//   {
//     char buffer [SECTOR_SIZE];

//     assert ( vol . read ( i, buffer, 1 ) );
//     assert ( vol . write ( i, buffer, 1 ) );
//   }

//   /* Extensive testing of your RAID implementation ...
//    */


//   /* Stop the raid device ...
//    */
//   assert ( vol . stop () == RAID_STOPPED );
//   assert ( vol . status () == RAID_STOPPED );

//   /* ... and the underlying disks.
//    */

//   doneDisks ();

//   printf("test1 done\n");
// }

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
//-------------------------------------------------------------------------------------------------
// void                                   test2                                   ()
// {
//   /* The RAID as well as disks are stopped. It corresponds i.e. to the
//    * restart of a real computer.
//    *
//    * after the restart, we will not create the disks, nor create RAID (we do not
//    * want to destroy the content). Instead, we will only open/start the devices:
//    */

//   TBlkDev dev = openDisks ();
//   CRaidVolume vol;

//   assert ( vol . start ( dev ) == RAID_OK );


//   /* some I/O: RaidRead/RaidWrite
//    */

//   vol . stop ();
//   doneDisks ();

//    printf("test2 done\n");
// }
//-------------------------------------------------------------------------------------------------


void printBuffer(const char* label, const char* buffer, int size) {
    std::cout << label << ":\n";
    for (int i = 0; i < size; ++i) {
        if (i % SECTOR_SIZE == 0 && i != 0) {
            std::cout << "\n";
        }
        std::cout << std::hex << std::setfill('0') << std::setw(2) << (buffer[i] & 0xFF) << " ";
    }
    std::cout << std::dec << "\n";
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

void test4_1 ()
{
    printf("test4 read degrade\n");
    TBlkDev  dev = createDisks ();
    assert ( CRaidVolume::create ( dev ) );
    CRaidVolume vol;
    assert ( vol . start ( dev ) == RAID_OK );
    assert ( vol . status () == RAID_OK );
    int size = 5;
   
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
	 //read failed here.
    assert(vol.read( 1, buffer, size));
    assert(vol.status() == RAID_DEGRADED);
   
	// print buffer and reference_buffer.

	printBuffer("Buffer after degrade", buffer, SECTOR_SIZE * size);
	printBuffer("Buffer after degrade", reference_buffer, SECTOR_SIZE * size);

    assert(memcmp(buffer, reference_buffer, SECTOR_SIZE*size) == 0);
    assert(vol.status() == RAID_DEGRADED);

    assert( vol . stop () == RAID_STOPPED );
    assert( vol . status () == RAID_STOPPED );
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


void test_two_disk_fail()
{
    printf("test4 read degrade\n");
    TBlkDev  dev = createDisks ();
    assert ( CRaidVolume::create ( dev ) );
    CRaidVolume vol;
    assert ( vol . start ( dev ) == RAID_OK );
    assert ( vol . status () == RAID_OK );
    int size = 6;
   
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
	 //read failed here.
    assert(vol.read( 1, buffer, size));
    assert(vol.status() == RAID_DEGRADED);
   
	// print buffer and reference_buffer.

	// printBuffer("Buffer after degrade", buffer, SECTOR_SIZE * size);
	// printBuffer("Buffer after degrade", reference_buffer, SECTOR_SIZE * size);

    assert(memcmp(buffer, reference_buffer, SECTOR_SIZE*size) == 0);
    assert(vol.status() == RAID_DEGRADED);

	degradeDisk(1);
	//read failed here.
    assert(vol.read( 1, buffer, size));
    assert(vol.status() == RAID_DEGRADED);

	printBuffer("Buffer after degrade", buffer, SECTOR_SIZE * size);
	printBuffer("Buffer after degrade", reference_buffer, SECTOR_SIZE * size);

    assert(memcmp(buffer, reference_buffer, SECTOR_SIZE*size) == 0);
    assert(vol.status() == RAID_DEGRADED);

    assert( vol . stop () == RAID_STOPPED );
    assert( vol . status () == RAID_STOPPED );
    doneDisks ();
}






int                                    main                                    ()
{
	// test0();
    // test_read();
    // test_read_error();
    // test1 ();
    // test2 ();
    // test_multi_write();
    // test3();
    // test4_1();
	// test4();
    // test5();
	// test_resync();
    // test_resync_whole();
    // test_offline_replace();
	test_two_disk_fail();
	return EXIT_SUCCESS;
}

#endif /* __PROGTEST__ */
