#include "hmicx.h"
#include <lz4.h>
#include <lz4hc.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <map>
#include <cstring>
#include <algorithm>
#include <cstdint>

using namespace std;
using namespace HMICX;

struct RGBA { uint8_t r,g,b,a; };

// Helper functions to write little-endian values
static void writeU8(ofstream& out, uint8_t val) {
    out.write((char*)&val, 1);
}

static void writeU16(ofstream& out, uint16_t val) {
    uint8_t bytes[2] = {
        (uint8_t)(val & 0xFF),
        (uint8_t)((val >> 8) & 0xFF)
    };
    out.write((char*)bytes, 2);
}

static void writeU32(ofstream& out, uint32_t val) {
    uint8_t bytes[4] = {
        (uint8_t)(val & 0xFF),
        (uint8_t)((val >> 8) & 0xFF),
        (uint8_t)((val >> 16) & 0xFF),
        (uint8_t)((val >> 24) & 0xFF)
    };
    out.write((char*)bytes, 4);
}

struct FrameIndexEntry {
    uint32_t offset;
    uint32_t size;
    uint8_t  type;
};

static RGBA parseColor(const string& s) {
    RGBA c{255,255,255,255};
    string str = s;
    transform(str.begin(), str.end(), str.begin(), ::tolower);
    if (str.rfind("#",0)==0 && str.size()==7) {
        c.r = stoi(str.substr(1,2),nullptr,16);
        c.g = stoi(str.substr(3,2),nullptr,16);
        c.b = stoi(str.substr(5,2),nullptr,16);
    } else if (str.find("rgba(")==0) {
        int r,g,b,a;
        if (sscanf(str.c_str(),"rgba(%d,%d,%d,%d)",&r,&g,&b,&a)==4)
            c={uint8_t(r),uint8_t(g),uint8_t(b),uint8_t(a)};
    } else if (str.find("rgb(")==0) {
        int r,g,b;
        if (sscanf(str.c_str(),"rgb(%d,%d,%d)",&r,&g,&b)==3)
            c={uint8_t(r),uint8_t(g),uint8_t(b),255};
    }
    return c;
}

static vector<vector<RGBA>> renderAllFrames(
        const vector<Command>& commands,int width,int height,int totalFrames) {
    cout<<"[DEBUG] 🎨 Rendering "<<totalFrames<<" frames ("<<width<<"x"<<height<<")...\n";
    cout<<"[DEBUG] 🎨 Processing "<<commands.size()<<" commands...\n";
    
    vector<vector<RGBA>> frames(totalFrames, vector<RGBA>(width*height,{0,0,0,0}));
    
    int pixelsDrawn = 0;
    int commandsProcessed = 0;
    int pixelsSkippedOutOfBounds = 0;
    int pixelsSkippedWrongFrame = 0;
    
    for (size_t cmdIdx = 0; cmdIdx < commands.size(); cmdIdx++) {
        const auto& cmd = commands[cmdIdx];
        RGBA color=parseColor(cmd.color);
        
        int cmdStart = cmd.start;
        int cmdEnd = cmd.end;
        
        if (cmdIdx < 3) {
            cout<<"[DEBUG] 🔍 Command "<<cmdIdx<<": color="<<cmd.color
                <<" (parsed as r="<<(int)color.r<<",g="<<(int)color.g<<",b="<<(int)color.b<<",a="<<(int)color.a<<")"
                <<", frames="<<cmdStart<<"-"<<cmdEnd
                <<", pixels="<<cmd.pixels.size()<<"\n";
        }
        
        for (int f=cmdStart; f<=cmdEnd && f<=totalFrames; ++f) {
            int idx = f - 1;
            
            if(idx < 0 || idx >= totalFrames) {
                pixelsSkippedWrongFrame += cmd.pixels.size();
                if (cmdIdx < 3) {
                    cout<<"[DEBUG]   ⚠️ Frame "<<f<<" (idx="<<idx<<") out of range [0,"<<(totalFrames-1)<<"]!!\n";
                }
                continue;
            }
            
            if (cmdIdx < 3) {
                cout<<"[DEBUG]   ✅ Processing frame "<<f<<" (idx="<<idx<<")\n";
            }
            
            for (const auto& px:cmd.pixels) {
                int x=px.x-1, y=px.y-1;
                
                if (x<0||x>=width||y<0||y>=height) {
                    pixelsSkippedOutOfBounds++;
                    if (cmdIdx < 3) {
                        cout<<"[DEBUG]     ⚠️ Pixel ("<<px.x<<","<<px.y<<") -> ("<<x<<","<<y<<") out of bounds!!\n";
                    }
                    continue;
                }
                
                int i=y*width+x;
                
                if (color.a==255) {
                    frames[idx][i]=color;
                    pixelsDrawn++;
                } else if (color.a>0) {
                    RGBA& bg=frames[idx][i];
                    float a=color.a/255.f, ia=1.f-a;
                    bg.r=uint8_t(color.r*a+bg.r*ia);
                    bg.g=uint8_t(color.g*a+bg.g*ia);
                    bg.b=uint8_t(color.b*a+bg.b*ia);
                    bg.a=max(bg.a,color.a);
                    pixelsDrawn++;
                }
            }
            commandsProcessed++;
        }
    }
    
    cout<<"[DEBUG] 🎨 Drew "<<pixelsDrawn<<" pixels total\n";
    cout<<"[DEBUG] 🎨 Commands processed: "<<commandsProcessed<<"\n";
    cout<<"[DEBUG] 🎨 Pixels skipped (out of bounds): "<<pixelsSkippedOutOfBounds<<"\n";
    cout<<"[DEBUG] 🎨 Pixels skipped (wrong frame): "<<pixelsSkippedWrongFrame<<"\n";
    
    int nonBlackInFrame0 = 0;
    for (const auto& pixel : frames[0]) {
        if (pixel.r > 0 || pixel.g > 0 || pixel.b > 0 || pixel.a > 0) {
            nonBlackInFrame0++;
        }
    }
    cout<<"[DEBUG] 🎨 Non-black pixels in frame 0: "<<nonBlackInFrame0<<" / "<<frames[0].size()<<"\n";
    
    cout<<"[DEBUG] 🎨 Sample pixels from frame 0:\n";
    for (int i = 0; i < min(10, (int)frames[0].size()); i++) {
        auto& p = frames[0][i];
        if (p.r > 0 || p.g > 0 || p.b > 0 || p.a > 0) {
            cout<<"[DEBUG]   Pixel "<<i<<": r="<<(int)p.r<<" g="<<(int)p.g<<" b="<<(int)p.b<<" a="<<(int)p.a<<"\n";
        }
    }
    
    if (pixelsDrawn == 0) {
        cout<<"[WARNING] ⚠️⚠️⚠️ NO PIXELS DRAWN!! Output will be BLACK!!\n";
    } else if (nonBlackInFrame0 == 0) {
        cout<<"[WARNING] ⚠️⚠️⚠️ PIXELS WERE DRAWN BUT FRAME 0 IS ALL BLACK!!\n";
    }
    
    return frames;
}

static void computeDelta(
        const vector<RGBA>& prev,const vector<RGBA>& curr,int width,
        vector<uint8_t>& deltaData) {
    
    size_t changeCount = 0;
    for (size_t i=0;i<curr.size();++i) {
        if (memcmp(&prev[i],&curr[i],sizeof(RGBA))!=0) {
            changeCount++;
        }
    }
    
    deltaData.resize(4 + changeCount * 8);
    
    deltaData[0] = (changeCount & 0xFF);
    deltaData[1] = ((changeCount >> 8) & 0xFF);
    deltaData[2] = ((changeCount >> 16) & 0xFF);
    deltaData[3] = ((changeCount >> 24) & 0xFF);
    
    size_t offset = 4;
    for (size_t i=0;i<curr.size();++i) {
        if (memcmp(&prev[i],&curr[i],sizeof(RGBA))!=0) {
            uint16_t x = i % width;
            uint16_t y = i / width;
            
            deltaData[offset++] = x & 0xFF;
            deltaData[offset++] = (x >> 8) & 0xFF;
            deltaData[offset++] = y & 0xFF;
            deltaData[offset++] = (y >> 8) & 0xFF;
            deltaData[offset++] = curr[i].r;
            deltaData[offset++] = curr[i].g;
            deltaData[offset++] = curr[i].b;
            deltaData[offset++] = curr[i].a;
        }
    }
}

static void writeHMICB(const string& path, int width, int height, int fps, 
                       int totalFrames, bool loop,
                       const vector<vector<RGBA>>& frames) {
    cout<<"[DEBUG] 💾 Writing "<<path<<"...\n";
    ofstream out(path,ios::binary);
    if(!out) throw runtime_error("cannot open output");

    out.write("HMICB", 5);
    writeU8(out, 1);
    writeU16(out, (uint16_t)width);
    writeU16(out, (uint16_t)height);
    writeU16(out, (uint16_t)fps);
    writeU32(out, (uint32_t)totalFrames);
    writeU8(out, loop ? 1 : 0);
    writeU8(out, 1);
    
    for(int i = 0; i < 14; i++) {
        writeU8(out, 0);
    }
    
    streampos afterHeader = out.tellp();
    cout<<"[DEBUG] After header: byte "<<afterHeader<<" (should be 32)\n";

    uint32_t indexSize = frames.size() * 9;
    uint32_t dataStartOffset = 32 + indexSize;
    
    cout<<"[DEBUG] Index size: "<<indexSize<<" bytes\n";
    cout<<"[DEBUG] Frame data will start at byte: "<<dataStartOffset<<"\n";

    streampos indexPos = out.tellp();
    for (size_t i = 0; i < frames.size(); i++) {
        writeU32(out, 0);
        writeU32(out, 0);
        writeU8(out, 0);
    }
    
    streampos dataStart = out.tellp();
    cout<<"[DEBUG] Data actually starts at byte "<<dataStart<<" (should be "<<dataStartOffset<<")\n";
    
    if((uint32_t)dataStart != dataStartOffset) {
        throw runtime_error("MATH ERROR!! Data start position mismatch!!");
    }

    vector<FrameIndexEntry> index(frames.size());
    size_t totalOrig=0, totalOut=0;

    for(size_t i=0;i<frames.size();++i){
        streampos pos = out.tellp();
        index[i].offset = (uint32_t)pos;
        
        const auto& frame = frames[i];
        size_t frameSize = frame.size() * sizeof(RGBA);
        totalOrig += frameSize;

        if(i == 0 || i % 10 == 0){
            out.write((char*)frame.data(), frameSize);
            index[i].size = (uint32_t)frameSize;
            index[i].type = 0;
            totalOut += frameSize;
            
            if(i == 0) {
                cout<<"[DEBUG] Frame 0 written at byte "<<pos
                    <<", size="<<frameSize<<" bytes (full frame)\n";
            }
        } else {
            vector<uint8_t> deltaData;
            computeDelta(frames[i-1], frame, width, deltaData);
            
            out.write((char*)deltaData.data(), deltaData.size());
            index[i].size = (uint32_t)deltaData.size();
            index[i].type = 1;
            totalOut += deltaData.size();
            
            if(i == 1) {
                cout<<"[DEBUG] Frame 1 written at byte "<<pos
                    <<", size="<<deltaData.size()<<" bytes (delta frame)\n";
            }
        }

        if(i < 3 || i == frames.size() - 1) {
            cout<<"[DEBUG] Frame "<<i
                <<": offset="<<index[i].offset
                <<", size="<<index[i].size
                <<", type="<<(int)index[i].type<<"\n";
        }
    }

    out.seekp(indexPos);
    cout<<"[DEBUG] Backpatching index at byte "<<indexPos<<"...\n";
    
    for (size_t i = 0; i < index.size(); i++) {
        writeU32(out, index[i].offset);
        writeU32(out, index[i].size);
        writeU8(out, index[i].type);
    }
    
    cout<<"[DEBUG] Index backpatched!!\n";
    cout<<"[DEBUG] First frame index entry: offset="<<index[0].offset
        <<", size="<<index[0].size
        <<", type="<<(int)index[0].type<<"\n";
    
    out.close();

    cout<<"[DEBUG] Delta compression: "<<totalOrig<<" → "<<totalOut
        <<" bytes ("<<(totalOrig > 0 ? 100.0*(1.0-totalOut/(double)totalOrig) : 0)<<"% saved)\n";
}

// 🔥🔥 LZ4 COMPRESSION GO BRRRRR!! 🚀🚀
static void compressToHMICB7(const string& hmicbPath, const string& hmicb7Path) {
    cout<<"\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    cout<<"⚡ LZ4 COMPRESSION (HC MODE) ⚡\n";
    cout<<"   SPEEDRUN STRATS ACTIVATED!! 🏃💨\n";
    cout<<"━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    
    // Read the uncompressed HMICB file
    ifstream in(hmicbPath, ios::binary | ios::ate);
    if(!in) throw runtime_error("cannot open HMICB file for compression");
    
    streamsize fileSize = in.tellg();
    in.seekg(0);
    
    vector<char> uncompressedData(fileSize);
    in.read(uncompressedData.data(), fileSize);
    in.close();
    
    cout<<"[DEBUG] 📖 Read "<<fileSize<<" bytes from "<<hmicbPath<<"\n";
    
    // Get max compressed size bound (LZ4 needs this for buffer allocation)
    int maxCompressedSize = LZ4_compressBound(fileSize);
    vector<char> compressedData(maxCompressedSize);
    
    cout<<"[DEBUG] 🔨 Compressing with LZ4 HC (high compression mode)... LETS GOOOO!!\n";
    
    // Use LZ4_compress_HC for better compression (level 9 = max compression)
    // If you want SPEED instead of compression, use LZ4_compress_default()
    int compressedSize = LZ4_compress_HC(
        uncompressedData.data(),
        compressedData.data(),
        fileSize,
        maxCompressedSize,
        LZ4HC_CLEVEL_MAX  // 🔥 MAX COMPRESSION LEVEL!! (level 12)
    );
    
    if(compressedSize <= 0) {
        throw runtime_error("LZ4 compression failed!! Yikes!! 💀");
    }
    
    // Write compressed data to .hmicb7
    ofstream out(hmicb7Path, ios::binary);
    if(!out) throw runtime_error("cannot create HMICB7 output file");
    
    // Store original size first (8 bytes for decompression)
    uint64_t origSize = fileSize;
    out.write((char*)&origSize, sizeof(uint64_t));
    
    // Write compressed data
    out.write(compressedData.data(), compressedSize);
    out.close();
    
    size_t totalWritten = compressedSize + sizeof(uint64_t);
    double ratio = 100.0 * (1.0 - (double)totalWritten / (double)fileSize);
    
    cout<<"[DEBUG] 💾 Wrote "<<totalWritten<<" bytes to "<<hmicb7Path<<"\n";
    cout<<"[DEBUG]    (original size header: 8 bytes, compressed data: "<<compressedSize<<" bytes)\n";
    cout<<"[DEBUG] 📊 Compression ratio: "<<fileSize<<" → "<<totalWritten
        <<" bytes ("<<ratio<<"% smaller)\n";
    cout<<"[DEBUG] ⚡ LZ4 was probably WAY faster than ZSTD btw!! No cap!! 🚀\n";
    cout<<"━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
}

int main(){
    cout<<"━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    cout<<"🎤 HMIC → HMICB/HMICB7 CONVERTER v6.0 🎤\n";
    cout<<"   NOW WITH LZ4 COMPRESSION!! ⚡⚡⚡\n";
    cout<<"   (ZSTD? NAH WE GOT THAT SPEEEEED) 🚀\n";
    cout<<"━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n";
    
    string input; 
    cout<<"📂 Enter HMIC/HMIC7 file path: "; 
    getline(cin,input);
    
    string outputFormat;
    cout<<"📦 Output format (1=HMICB, 2=HMICB7, 3=BOTH): ";
    getline(cin, outputFormat);
    
    bool createHMICB = (outputFormat == "1" || outputFormat == "3");
    bool createHMICB7 = (outputFormat == "2" || outputFormat == "3");
    
    if(!createHMICB && !createHMICB7) {
        cout<<"[WARNING] Invalid choice, defaulting to BOTH formats!!\n";
        createHMICB = createHMICB7 = true;
    }
    
    try{
        bool compressed = (input.size()>=6 &&
            input.substr(input.size()-6)==".hmic7");
        string parsePath=input;
        string temp=".tmp.hmic";
        
        if(compressed){
            cout<<"[DEBUG] 📦 Decompressing HMIC7 with LZ4...\n";
            ifstream in(input,ios::binary|ios::ate);
            if(!in) throw runtime_error("no input");
            
            streamsize sz=in.tellg(); 
            in.seekg(0);
            
            // Read original size (first 8 bytes)
            uint64_t originalSize;
            in.read((char*)&originalSize, sizeof(uint64_t));
            
            // Read compressed data
            size_t compressedSize = sz - sizeof(uint64_t);
            vector<char> compressedBuf(compressedSize);
            in.read(compressedBuf.data(), compressedSize);
            in.close();
            
            // Decompress!!
            vector<char> outBuf(originalSize);
            int decompSize = LZ4_decompress_safe(
                compressedBuf.data(),
                outBuf.data(),
                compressedSize,
                originalSize
            );
            
            if(decompSize < 0) {
                throw runtime_error("LZ4 decompression failed!! RIP!! 💀");
            }
            
            cout<<"[DEBUG] ✅ Decompressed "<<compressedSize<<" → "<<decompSize<<" bytes\n";
            
            ofstream t(temp,ios::binary);
            t.write(outBuf.data(),decompSize); 
            t.close();
            parsePath=temp;
        }

        cout<<"[DEBUG] 📖 Parsing HMIC file...\n";
        Parser p(parsePath); 
        p.parse();
        
        auto h=p.getHeader(); 
        auto cmds=p.getCommands();
        
        cout<<"\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
        cout<<"🔍 PARSER OUTPUT ANALYSIS\n";
        cout<<"━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
        
        cout<<"[DEBUG] Total commands from parser: "<<cmds.size()<<"\n";
        
        int emptyCount = 0;
        int totalPixels = 0;
        for (size_t i = 0; i < cmds.size(); i++) {
            if (cmds[i].pixels.empty()) {
                emptyCount++;
            } else {
                totalPixels += cmds[i].pixels.size();
            }
            
            if (i < 5) {
                cout<<"[DEBUG] Command "<<i<<": color="<<cmds[i].color
                    <<", pixels="<<cmds[i].pixels.size()
                    <<", frames="<<cmds[i].start<<"-"<<cmds[i].end<<"\n";
            }
        }
        
        cout<<"[DEBUG] Commands with pixels: "<<(cmds.size() - emptyCount)<<"\n";
        cout<<"[DEBUG] Commands WITHOUT pixels: "<<emptyCount<<"\n";
        cout<<"[DEBUG] Total pixels across all commands: "<<totalPixels<<"\n";
        
        if (emptyCount > 0) {
            cout<<"[WARNING] ⚠️⚠️⚠️ Found "<<emptyCount<<" commands with 0 pixels!!\n";
        }
        
        vector<Command> validCmds;
        for (const auto& cmd : cmds) {
            if (!cmd.pixels.empty()) {
                validCmds.push_back(cmd);
            }
        }
        
        cout<<"[DEBUG] ✂️ Filtered "<<cmds.size()<<" → "<<validCmds.size()<<" valid commands\n";
        
        if (validCmds.empty()) {
            throw runtime_error("💀 NO VALID COMMANDS WITH PIXELS!! Check your HMIC parser!! 💀");
        }
        
        cmds = validCmds;
        
        cout<<"━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n";
        
        int width=5, height=5, fps=2, frames=1; 
        bool loop=true;
        
        for(auto& [k,v]:h){
            string key=k; 
            transform(key.begin(),key.end(),key.begin(),::toupper);
            if(key=="DISPLAY") {
                if(sscanf(v.c_str(),"%dx%d",&width,&height) != 2) {
                    if(sscanf(v.c_str(),"%dX%d",&width,&height) != 2) {
                        cout<<"[WARNING] Failed to parse DISPLAY: "<<v<<"\n";
                    } else {
                        cout<<"[DEBUG] Parsed DISPLAY with uppercase X: "<<width<<"x"<<height<<"\n";
                    }
                } else {
                    cout<<"[DEBUG] Parsed DISPLAY: "<<width<<"x"<<height<<"\n";
                }
            }
            else if(key=="FPS") fps=stoi(v);
            else if(key=="F") frames=stoi(v);
            else if(key=="LOOP") loop=(v=="Y"||v=="y"||v=="1");
        }

        cout<<"━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
        cout<<"📊 ANIMATION PROPERTIES\n";
        cout<<"━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
        cout<<"  Size: "<<width<<"x"<<height<<"\n";
        cout<<"  FPS: "<<fps<<"\n";
        cout<<"  Frames: "<<frames<<"\n";
        cout<<"  Loop: "<<(loop?"yes":"no")<<"\n";
        cout<<"  Commands: "<<cmds.size()<<"\n";
        cout<<"━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n";

        if(width <= 0 || height <= 0 || width > 10000 || height > 10000) {
            throw runtime_error("Invalid dimensions!");
        }

        auto fr=renderAllFrames(cmds,width,height,frames);
        
        string base=input.substr(0,input.find_last_of('.'));
        string hmicbFile = base + ".hmicb";
        string hmicb7File = base + ".hmicb7";
        
        // Always create HMICB first (we need it for compression)
        writeHMICB(hmicbFile, width, height, fps, frames, loop, fr);
        
        // If they want HMICB7, compress it with LZ4!!
        if(createHMICB7) {
            compressToHMICB7(hmicbFile, hmicb7File);
        }
        
        // If they ONLY wanted HMICB7, delete the uncompressed version
        if(createHMICB7 && !createHMICB) {
            cout<<"[DEBUG] 🗑️  Removing intermediate HMICB file (keeping only HMICB7)...\n";
            remove(hmicbFile.c_str());
        }
        
        if(compressed) remove(temp.c_str());
        
        cout<<"\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
        cout<<"✅ SUCCESS!! Created:\n";
        if(createHMICB) cout<<"   📄 "<<hmicbFile<<" (uncompressed)\n";
        if(createHMICB7) cout<<"   ⚡ "<<hmicb7File<<" (LZ4 HC compressed)\n";
        cout<<"🔥 LZ4 GO BRRRRR WE COOKIN FR FR!! 🚀\n";
        cout<<"━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
        
    }catch(const exception& e){ 
        cerr<<"\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
        cerr<<"❌ ERROR: "<<e.what()<<"\n"; 
        cerr<<"━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
        return 1; 
    }
    
    return 0;
}