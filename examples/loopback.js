/**
 * Loopback filesystem
 **/

"use strict";
const FileSystem = require('fusejs').FileSystem;
const PosixError = require('fusejs').PosixError;
const pth = require('path');
const fs = require('fs');
// since the inodes need to be unique, 
// we need to keep track of the inodes and it's associated path
const pathToInode = new Map(); 
const inodeToPath = new Map();
inodeToPath.set( 1, '/');
var next_largest_inode = 2;

// variable to store the loopback folder
// this will be set later
var loopbackFolder = ""; 

function parseFileInfoReadWrite(fileInfo){
    if(fileInfo.rdonly){
        return 'r';
    }
    if(fileInfo.wronly){
        return 'w';
    }
    if(fileInfo.rdwr){
        return 'r+';
    }

}

class LoopbackFS extends FileSystem {
    lookup(context, parentInode, name, reply){

        // get the parent path
        const parent = inodeToPath.get(parentInode);

        // make sure it exists
        if( !parent ){
            reply.err(PosixError.ENOENT);
            return;
        }

        // get the full folder path
        const localPath = pth.join(parent,name);
        const path = pth.join(loopbackFolder, parent, name);

        // get the file information 
        fs.stat( path, function(err, stat){
            if(err){
                reply.err(-err.errno);
                return;
            }

            // check to see if the path has been visited before.
            // if not, add it to the map
            var inode = 0;
            if( ! pathToInode.has(localPath) ){
                inode = next_largest_inode;
                pathToInode[localPath] = inode;
                inodeToPath[inode] = localPath;
                next_largest_inode++;
            }else{
                inode = pathToInode.get(localPath);
            }

            stat.inode = stat.ino;
            const entry = {
                inode, 
                attr: stat,
                generation: 1 //some filesystems rely on this generation number, such as the  Network Filesystem
            };

        });

    }

    getattr(context, inode, reply){
        const path = inodeToPath.get(inode);
        if(path){
            fs.stat(path, function(err,stat){
                if(err){
                    reply.err(-err.errno);
                    return;
                }
                reply.attr(stat,5); //5, timeout value, in seconds, for the validity of this inode. so 5 seconds
            });

        }else{
            reply.err(PosixError.ENOENT); 
        }
        return;
    }
    releasedir(context, inode, fileInfo, reply){
        // console.log('Releasedir was called!');
        // console.log(fileInfo);
        reply.err(0);
    }

    opendir(context, inode, fileInfo, reply){
        reply.open(fileInfo);
    }


    readdir(context, inode, requestedSize, offset, fileInfo, reply){
        //http://fuse.sourceforge.net/doxygen/structfuse__lowlevel__ops.html#af1ef8e59e0cb0b02dc0e406898aeaa51
        
        /*
        Read directory
        Send a buffer filled using reply.addDirEntry. Send an empty buffer on end of stream.
        fileInfo.fh will contain the value set by the opendir method, or will be undefined if the opendir method didn't set any value.
        Returning a directory entry from readdir() does not affect its lookup count.
        Valid replies: reply.addDirEntry reply.buffer, reply.err
        */

        /*
        size is the maximum memory size of the buffer for the underlying fuse
        filesystem. currently this cannot be determined a priori
        */

        const folder = inodeToPath.get(inode);
        if(!folder){
            reply.err(PosixError.ENOENT);
            return;
        }        

        const path = pth.join(loopbackFolder, folder);
        fs.readdir(path, function(err, files){

            if(err){
                reply.err(-err.errno);
                return;
            }

            const size = Math.max( requestedSize , files.length * 256);
            for( let file of files){
                let attr = fs.lstatSync(pth.join(path, file));
                attr.atime = attr.atime.getTime();
                attr.mtime = attr.mtime.getTime();
                attr.ctime = attr.ctime.getTime();
                attr.inode = attr.ino;
                attr.birthtime = attr.birthtime.getTime();

                // keep track of new inodes
                if( !inodeToPath.has(attr.ino)){                    
                    inodeToPath.set(attr.ino, pth.join(folder, file));
                }
                reply.addDirEntry(file, size, attr , offset);   
            }
            reply.buffer(new Buffer(0), requestedSize)

        });

    }

    open(context, inode, fileInfo, reply){
        const filePath = inodeToPath.has(inode);
        if(file){
            const flag = parseFileInfoReadWrite(fileInfo);

            fs.open(filePath, flag, function(err,fd){
                if(err){
                    reply.err(err.code);
                    return;
                }

                fileInfo.file_handle = fd;
                reply.open(fileInfo);
                return;
            })

            return;

        }

        reply.err(PosixError.ENOENT);

        return;

    }

    read(context, inode, len, offset, fileInfo, reply){
        if(inode == 3){
            const length = file_content.length
            const content = file_content.substr(offset,Math.min(length, offset + len));
            reply.buffer(new Buffer(content), content.length);
            return;
        }

        reply.err(PosixError.ENOENT);
        return;
    }

    release(context, inode, fileInfo, reply){
        reply.err(0);
    }

};

function setLoopback(folder){
    loopbackFolder = folder;
}

module.exports = {LoopbackFS, setLoopback};
