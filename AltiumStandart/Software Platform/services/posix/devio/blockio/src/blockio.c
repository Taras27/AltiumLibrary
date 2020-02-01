/*****************************************************************************
|*
|*  Copyright:      Copyright (c) 2008 Altium
|*
|*  Description:    Generic interface for block devices.
|*
\*****************************************************************************/
#include <limits.h>

#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>

#include <blockio.h>
#include <devio.h>
#include <devio_adaptor.h>
#include <sysutils.h>
#include <util_endian.h>

#ifdef BLOCKIO_DEBUG
#include <stdio.h>
#endif

#define BLOCKIO_DEFAULT_SECTORSIZE              512

/**
 * @brief
 *      Write a number of sectors to the block device
 *
 * This function writes @em size sectors of data from @buf starting at
 * sector @em lba.
 *
 * @param fd
 *      File descriptor
 * @param buf
 *      Pointer to data source
 * @param lba
 *      Sector to start writing
 * @param size
 *      Number of sectors to write
 *
 * @return
 *      Number of sectors written, or -1 on failure.
 */
extern ssize_t bwrite( int fd, const void *buf, uint32_t lba, size_t size )
{
    posix_devctl_blockio_impl_t impl;

    if ( posix_devctl( fd, DEVCTL_BLOCKIO_IMPL, ( void * ) &impl, sizeof( impl ), NULL ) < 0 )
    {
        return BLOCKIO_ERROR_IO;
    }
    if ( buf == NULL )
    {
        return BLOCKIO_ERROR_IO;
    }

    return impl.write_sectors( impl.device, buf, lba, size );
}

/**
 * @brief
 *      Clear a number of sectors on the block device
 *
 * This function writes zeros to @em size sectors of the block device starting
 * at sector @em lba.
 *
 * @param fd
 *      File descriptor
 * @param lba
 *      Sector to start writing
 * @param size
 *      Number of sectors to clear
 *
 * @return
 *      Number of sectors cleared, or -1 on failure.
 */
extern ssize_t bclear( int fd, uint32_t lba, size_t size )
{
    posix_devctl_blockio_impl_t impl;

    if ( posix_devctl( fd, DEVCTL_BLOCKIO_IMPL, ( void * ) &impl, sizeof( impl ), NULL ) < 0 )
    {
        return BLOCKIO_ERROR_IO;
    }

    return impl.clear_sectors( impl.device, lba, size );
}

/**
 * @brief
 *      Read a number of sectors from the block device
 *
 * This function reads @em size sectors of data starting at sector @em lba.
 * Buffer @em buf must be large enough to hold the requested number of sectors.
 *
 * @param fd
 *      File descriptor
 * @param buf
 *      Pointer to data destination
 * @param lba
 *      Sector to start reading
 * @param size
 *      Number of sectors to read.
 *
 * @return
 *      Number of sectors read, or -1 on failure.
 */
extern ssize_t bread( int fd, void *buf, uint32_t lba, size_t size )
{
    posix_devctl_blockio_impl_t impl;

    if ( posix_devctl( fd, DEVCTL_BLOCKIO_IMPL, ( void * ) &impl, sizeof( impl ), NULL ) < 0 )
    {
        return BLOCKIO_ERROR_IO;
    }

    return impl.read_sectors( impl.device, buf, lba, size );
}

/**
 * @brief
 *      Flush cached data to disk
 *
 * This function makes sure all cached data is written to disk. The
 * @em invalidate argument determines if the cache is invalidated.
 *
 * @param fd
 *      File descriptor
 * @param invalidate
 *      False to keep cached data valid, true to invalidate the cached data
 *
 * @return
 *      0 if successful, or -1 otherwise.
 */
extern int bflush( int fd, bool invalidate )
{
    posix_devctl_blockio_impl_t impl;

    if ( posix_devctl( fd, DEVCTL_BLOCKIO_IMPL, ( void * ) &impl, sizeof( impl ), NULL ) < 0 )
    {
        return BLOCKIO_ERROR_IO;
    }

    return impl.flush_data( impl.device, invalidate ? 1 : 0 );
}

//.............................................................................
// Partition table support

// layout of a partition table
typedef struct
{
    uint8_t state;
    uint8_t begin_head;
    uint8_t begin_cylindersector[2];
    uint8_t type;
    uint8_t end_head;
    uint8_t end_cylindersector[2];
    uint8_t startsector[4];
    uint8_t sectors[4];
} blockio_mbr_partinfo_t;

// layout of a master boot record
typedef struct
{
    uint8_t bootcode[446];
    blockio_mbr_partinfo_t partition[4];
    uint8_t signature[2];
} blockio_mbr_t;

#define BLOCKIO_MBR_SIGNATURE   0xAA55
#define RESERVED_SECTORS        1

/**
 * @brief
 *      Write a new partition table to a device
 *
 * This function writes to the master boot record of a device an installs a
 * partition table. The @em size argument is a four entry array describing the
 * division of the disk in 4 partitions according to the following rules:
 * - size[n] == 0 means entry n+1 not used
 * - size[n] > 0 means entry n+1 uses absoluut size in MB
 * - size[n] < 0 means entry n+1 uses relative size
 *
 * For example:
 * @code
 * blockio_write_partitiontable( fd, { 10, -1, -4, 0 } );
 * @endcode
 *
 * This will:
 * - allocate 10 MB for partition 1
 * - allocate 1/5 of (disksize - 10 MB) for partition 2
 * - allocate 4/5 of (disksize - 10 MB) for partition 3
 * - mark partition 4 unused
 *
 *
 * @param fd
 *      Block device file descriptor
 * @param size
 *      4-entry array describing the partition division.
 *
 * @return
 *      Unallocated space in MB, or <0 on error.
 */
extern int blockio_write_partitiontable( int fd, int32_t size[] )
{
    int32_t sectorsleft;
    int relativetotal = 0;
    uint8_t i;
    uint32_t buf32[BLOCKIO_DEFAULT_SECTORSIZE / sizeof( uint32_t )];
    blockio_mbr_t *mbr = ( blockio_mbr_t * ) buf32;
    posix_devctl_blockio_cfg_t cfg;
    posix_devctl_blockio_impl_t impl;

    if ( posix_devctl( fd, DEVCTL_BLOCKIO_IMPL, ( void * ) &impl, sizeof( impl ), NULL ) < 0 )
    {
        return BLOCKIO_ERROR_IO;
    }

    if ( posix_devctl( fd, DEVCTL_BLOCKIO_CFG, ( void * ) &cfg, sizeof( cfg ), NULL ) < 0 )
    {
        return BLOCKIO_ERROR_IO;
    }

    sectorsleft = cfg.blktotal - RESERVED_SECTORS;
    if ( cfg.blksize != BLOCKIO_DEFAULT_SECTORSIZE )
    {
#ifdef BLOCKIO_DEBUG
        printf( "blocksize unsupported, aborting partition table init\n" );
#endif
        return BLOCKIO_ERROR_BLOCKSIZE_UNSUPPORTED;
    }

    // reserve absoluut sizes
    for ( i = 0; i < 4; ++i )
    {
        if ( size[i] > 0 )
        {
            size[i] *= ( 1024 * 1024 / BLOCKIO_DEFAULT_SECTORSIZE );
            sectorsleft -= size[i];
        }
        else
        {
            relativetotal -= size[i];
        }
    }

    if ( ( sectorsleft < 0 ) || ( !sectorsleft && relativetotal ) )
    {
#ifdef BLOCKIO_DEBUG
        printf( "disk full, aborting partition table init\n" );
#endif
        return BLOCKIO_ERROR_FDISK_DISKFULL;
    }

    // divide relative sizes
    uint64_t sectorstodivide = sectorsleft;
    for ( i = 0; i < 4; ++i )
    {
        if ( size[i] < 0 )
        {
            size[i] = - (int32_t)( ( ( (int64_t)sectorstodivide ) * size[i] / relativetotal ) & 0xFFFFFFFF );
            sectorsleft -= size[i];
        }
    }

    // write values into table
    memset( buf32, 0, BLOCKIO_DEFAULT_SECTORSIZE );

    uint32_t start = cfg.blkstart + RESERVED_SECTORS;
    for ( i = 0; i < 4; ++i )
    {
#ifdef BLOCKIO_DEBUG
        printf( "partition %i start: %i size: %i (%i Mb)\n", i + 1,
                start, size[i],
                ( uint32_t ) ( ( ( uint64_t ) size[i] ) * BLOCKIO_DEFAULT_SECTORSIZE ) / 1024 / 1024 );
#endif
        if ( size[i] )
        {
            write_little32to16( mbr->partition[i].startsector, start );
            write_little32to16( mbr->partition[i].sectors, size[i] );
            start += size[i];
        }
    }
#ifdef BLOCKIO_DEBUG
    printf( "\nslack %i sectors\n", sectorsleft );
#endif
    write_little16to16( mbr->signature, BLOCKIO_MBR_SIGNATURE );

    if ( impl.write_sectors( impl.device, buf32, 0, 1 ) != 1 )
    {
        return BLOCKIO_ERROR_IO;
    }

    return ( int32_t ) ( sectorsleft / ( 1024 * 1024 / BLOCKIO_DEFAULT_SECTORSIZE ) );
}

/**
 * @brief
 *      Write the partition type to a partition table
 *
 * This function writes the type of a given partition to a partition table
 *
 * @param fd
 *      Block device file descriptor
 * @param nr
 *      Partition number (1-4)
 * @param type
 *      Type of partition to create
 *
 * @return
 *      0 if successful, <>0 otherwise.
 */
extern int blockio_write_partitiontype( int fd, int nr, uint8_t type )
{
    uint32_t buf32[BLOCKIO_DEFAULT_SECTORSIZE / sizeof( uint32_t )];
    blockio_mbr_t *mbr = ( blockio_mbr_t * ) buf32;
    posix_devctl_blockio_cfg_t cfg;
    posix_devctl_blockio_impl_t impl;

    if ( posix_devctl( fd, DEVCTL_BLOCKIO_IMPL, ( void * ) &impl, sizeof( impl ), NULL ) < 0 )
    {
        return BLOCKIO_ERROR_IO;
    }

    if ( posix_devctl( fd, DEVCTL_BLOCKIO_CFG, ( void * ) &cfg, sizeof( cfg ), NULL ) < 0 )
    {
        return BLOCKIO_ERROR_IO;
    }

    if ( impl.read_sectors( impl.device, buf32, cfg.blkstart, 1 ) != 1 )
    {
        return BLOCKIO_ERROR_IO;
    }                           // device error

    mbr->partition[nr - 1].type = type & 0xFF;

    if ( impl.write_sectors( impl.device, buf32, cfg.blkstart, 1 ) != 1 )
    {
        return BLOCKIO_ERROR_IO;
    }                           // device error

    return 0;
}

/**
 * @brief
 *      Get limits for the specified partition
 *
 * This function reads the partition table on a device and fills in the
 * @em blkstart and @em blkcount arguments with the start and the size of the
 * specified partition.
 *
 * @param fd
 *      Blockio file descriptor
 * @param nr
 *      Partition number (1-4)
 * @param blkstart
 *      Location for start sector
 * @param blkcount
 *      Location for partition block count
 *
 * @return
 *      Type of partition, or <0 on error.
 */
extern int blockio_select_partition( int fd, int nr, uint32_t *blkstart, uint32_t *blkcount )
{
    uint32_t buf32[BLOCKIO_DEFAULT_SECTORSIZE / sizeof( uint32_t )];
    uint16_t *buf16 = ( uint16_t * ) buf32;
    blockio_mbr_t *mbr = ( blockio_mbr_t * ) buf32;
    blockio_mbr_partinfo_t *partinfo;
    posix_devctl_blockio_cfg_t cfg;
    posix_devctl_blockio_impl_t impl;

    if ( !blkstart || !blkcount )
    {
        return BLOCKIO_ERROR_IO;
    }
    *blkstart = *blkcount = 0;

    if ( nr < 1 || nr > 4 )
    {
        return BLOCKIO_ERROR_IO;
    }

    if ( posix_devctl( fd, DEVCTL_BLOCKIO_IMPL, ( void * ) &impl, sizeof( impl ), NULL ) < 0 )
    {
        return BLOCKIO_ERROR_IO;
    }

    if ( posix_devctl( fd, DEVCTL_BLOCKIO_CFG, ( void * ) &cfg, sizeof( cfg ), NULL ) < 0 )
    {
        return BLOCKIO_ERROR_IO;
    }

    // load MBR
    if ( impl.read_sectors( impl.device, buf32, cfg.blkstart, 1 ) != 1 )
    {
        return BLOCKIO_ERROR_IO;
    }

    // verify magic value
    if ( read_little16from16( mbr->signature ) != BLOCKIO_MBR_SIGNATURE )
    {
#ifdef BLOCKIO_DEBUG
        printf( "wrong magic value, cannot select partition\n" );
#endif
        return BLOCKIO_ERROR_WRONG_MBR_SIGNATURE;
    }

    // partition info
    partinfo = &mbr->partition[nr - 1];
    *blkstart = read_little32from16( partinfo->startsector );
    *blkcount = read_little32from16( partinfo->sectors );

#ifdef BLOCKIO_DEBUG
    printf( "partition %i type %02X\n", nr, partinfo->type );
    printf( "- start sector: %i\n", *blkstart );
    printf( "- number of sectors: %i\n", *blkcount );
#endif

    // basic sanity checks
	if (impl.sanity_check != NULL)
	{
		if (!impl.sanity_check(impl.device, *blkstart, *blkcount, cfg.blktotal))
		{
            *blkstart = *blkcount = 0;
            return BLOCKIO_ERROR_IO;
		}
	}
	else
	{
        if ( *blkstart > cfg.blktotal || *blkcount > cfg.blktotal - *blkstart )
		{
            *blkstart = *blkcount = 0;
            return BLOCKIO_ERROR_IO;
		}
    }

    return partinfo->type;
}
