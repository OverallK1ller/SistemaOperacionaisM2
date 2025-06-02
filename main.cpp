#include <iostream>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <vector>
#include <unordered_map>
#include <deque>
#include <iomanip>
#include <bitset>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <ctime>

using namespace std;

int PAGE_SIZE = 4096;
constexpr int TLB_SIZE = 16;
constexpr int PAGE_TABLE_ENTRIES_LVL1 = 1024;
constexpr int PAGE_TABLE_ENTRIES_LVL2 = 1024;
constexpr int PAGE_TABLE_ENTRIES_16 = 32;
constexpr int TOTAL_FRAMES = 128;

int totalAddresses = 0;
int tlbHits = 0;
int tlbMisses = 0;
int pageHits = 0;
int pageFaults = 0;
int dirtyWrites = 0;


struct PageTableEntry {
    bool valid = false;
    bool accessed = false;
    bool dirty = false;
    int frame = -1;
};

vector<bool> frames(TOTAL_FRAMES, false);
deque<int> frameQueue;
unordered_map<int, PageTableEntry*> frameToPage;
vector<vector<int>> physicalMemory(TOTAL_FRAMES, vector<int>(PAGE_SIZE, 0));

class TLB {
private:
    unordered_map<int, int> map;
    deque<int> lru;

public:
    int lookup(int virtualPage, bool &hit) {
        auto it = map.find(virtualPage);
        if (it != map.end()) {
            lru.erase(remove(lru.begin(), lru.end(), virtualPage), lru.end());
            lru.push_front(virtualPage);
            hit = true;
            return it->second;
        }
        hit = false;
        return -1;
    }

    void insert(int virtualPage, int physicalFrame) {
        if (map.size() == TLB_SIZE) {
            int leastUsed = lru.back();
            lru.pop_back();
            map.erase(leastUsed);
        }
        map[virtualPage] = physicalFrame;
        lru.push_front(virtualPage);
    }
};

void write_backing_store_value(int frameNumber, const string& filename = "backing_store.txt") {
    ofstream file(filename, ios::app);
    if (!file.is_open()) {
        cerr << "Erro ao abrir o arquivo backing_store.txt para escrita.\n";
        return;
    }

    file << "Frame " << frameNumber << " escrito de volta na backing store:\n";
    for (int i = 0; i < PAGE_SIZE; ++i) {
        file << physicalMemory[frameNumber][i] << "\n";
    }
    file << "---- Fim do frame " << frameNumber << " ----\n\n";
    file.close();
}

int allocate_frame() {
    for (int i = 0; i < TOTAL_FRAMES; ++i) {
        if (!frames[i]) {
            frames[i] = true;
            frameQueue.push_back(i);
            return i;
        }
    }

    if (!frameQueue.empty()) {
        int toReplace = frameQueue.front();
        frameQueue.pop_front();

        if (frameToPage.count(toReplace)) {
            PageTableEntry* oldEntry = frameToPage[toReplace];
            if (oldEntry->dirty) {
                write_backing_store_value(toReplace);
 dirtyWrites++;
            }
            oldEntry->valid = false;
            oldEntry->frame = -1;
            oldEntry->dirty = false;
            oldEntry->accessed = false;
        }

        frameQueue.push_back(toReplace);
        return toReplace;
    }

    return -1;
}

int read_memory_value(int physicalAddress, const string& dataFile = "data_memory.txt") {
    if (physicalAddress < 0 || physicalAddress >= TOTAL_FRAMES * PAGE_SIZE) {
        cerr << "Endereço físico inválido: " << physicalAddress << "\n";
        return -1;
    }

    ifstream file(dataFile);
    if (!file.is_open()) {
        cerr << "Erro ao abrir o arquivo de dados: " << dataFile << "\n";
        return -1;
    }

    string line;
    int currentLine = 0;
    while (getline(file, line)) {
        if (currentLine == physicalAddress) {
            istringstream iss(line);
            int value;
            if (iss >> value) {
                return value;
            } else {
                cerr << "Erro ao ler valor na linha " << currentLine << " do arquivo.\n";
                return -1;
            }
        }
        ++currentLine;
    }

    cerr << "Endereço físico fora do intervalo no arquivo de dados.\n";
    return -1;
}

int handle_page_fault(PageTableEntry &entry, int frameNumber = -1, const string& backingFile = "backing_store.txt") {
    if (frameNumber == -1) {
        frameNumber = allocate_frame();
        if (frameNumber == -1) {
            cerr << "Erro: Nenhum quadro disponível para alocação.\n";
            exit(1);
        }
    }

    ifstream file(backingFile);
    if (!file.is_open()) {
        cerr << "Erro ao abrir o arquivo de backing store.\n";
        exit(1);
    }

    string line;
    int lineIndex = 0;
    int baseLine = frameNumber * PAGE_SIZE;

    while (getline(file, line) && lineIndex < baseLine + PAGE_SIZE) {
        if (lineIndex >= baseLine) {
            istringstream iss(line);
            int value;
            if (iss >> value) {
                physicalMemory[frameNumber][lineIndex - baseLine] = value;
            }
        }
        ++lineIndex;
    }

    entry.valid = true;
    entry.frame = frameNumber;
    entry.accessed = true;
    entry.dirty = false;

    frameToPage[frameNumber] = &entry;

pageFaults++;

    return frameNumber;
}

bool is_hex(const string& s) {
    return s.size() > 2 && (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'));
}

unsigned int parse_address(const string& input) {
    unsigned int address;
    if (is_hex(input)) {
        stringstream ss;
        ss << hex << input;
        ss >> address;
    } else {
        address = stoi(input);
    }
    return address;
}

void process_address_16bit(unsigned int address, TLB& tlb, PageTableEntry pageTable[]) {
    unsigned int offset = address % PAGE_SIZE;
    unsigned int virtualPage = address / PAGE_SIZE;
    if (virtualPage >= PAGE_TABLE_ENTRIES_16) {
        cerr << "Endereço virtual excede a quantidade de páginas permitidas para 16 bits.\n";
        return;
    }

    bool tlbHit;
    int frame = tlb.lookup(virtualPage, tlbHit);

    cout << "Endereço virtual: " << address << "\n";
    cout << "Binário: " << bitset<16>(address) << "\n";
    cout << "Página: " << virtualPage << ", Offset: " << offset << "\n";

    if (tlbHit) {
        cout << "Ação: TLB hit\n";
tlbHits++;
    } else {
        cout << "Ação: TLB miss\n";
tlbMisses++;
        if (pageTable[virtualPage].valid) {
            frame = pageTable[virtualPage].frame;
            cout << "       Page hit\n";
  pageHits++;
        } else {
            cout << "       Page fault\n";
            frame = handle_page_fault(pageTable[virtualPage], -1);
            cout << "       Carregado da backing store\n";
    
        }
        tlb.insert(virtualPage, frame);
    }

    pageTable[virtualPage].accessed = true;

    if (rand() % 20 == 0) {
        pageTable[virtualPage].dirty = true;
    }

    int physicalAddress = frame * PAGE_SIZE + offset;
    int value = read_memory_value(physicalAddress);

    if (value == -1) {
        cout << "Valor lido: ERRO NA LEITURA\n";
    } else {
        cout << "Valor lido: " << value << "\n";
    }

    cout << "Accessed: " << pageTable[virtualPage].accessed
         << ", Dirty: " << pageTable[virtualPage].dirty << "\n";
    cout << "-----------------------------\n";
totalAddresses++;
}

void process_address_32bit(unsigned int address, TLB& tlb, PageTableEntry* level1Table[]) {
    unsigned int offset = address & (PAGE_SIZE - 1);
    unsigned int lvl2Index = (address >> 12) & 0x3FF;
    unsigned int lvl1Index = (address >> 22) & 0x3FF;
    int virtualPageNumber = (lvl1Index << 10) | lvl2Index;

    if (!level1Table[lvl1Index]) {
        try {
            level1Table[lvl1Index] = new PageTableEntry[PAGE_TABLE_ENTRIES_LVL2]();
        } catch (bad_alloc& e) {
            cerr << "Erro de alocação de memória: " << e.what() << "\n";
            exit(1);
        }
    }

    PageTableEntry &entry = level1Table[lvl1Index][lvl2Index];

    bool tlbHit;
    int frame = tlb.lookup(virtualPageNumber, tlbHit);

    cout << "Endereço virtual: " << address << "\n";
    cout << "Binário: " << bitset<32>(address) << "\n";
    cout << "PageDir: " << lvl1Index << ", PageTable: " << lvl2Index << ", Offset: " << offset << "\n";

    if (tlbHit) {
        cout << "Ação: TLB hit\n";
 tlbHits++;
    } else {
        cout << "Ação: TLB miss\n";
tlbMisses++;
        if (entry.valid) {
            frame = entry.frame;
            cout << "       Page hit\n";
 pageHits++;
        } else {
            cout << "       Page fault\n";
            frame = handle_page_fault(entry, -1);
            cout << "       Carregado da backing store\n";
    
        }
        tlb.insert(virtualPageNumber, frame);
    }

    entry.accessed = true;

    if (rand() % 20 == 0) {
        entry.dirty = true;
    }

    int physicalAddress = frame * PAGE_SIZE + offset;
    int value = read_memory_value(physicalAddress);

    if (value == -1) {
        cout << "Valor lido: ERRO NA LEITURA\n";
    } else {
        cout << "Valor lido: " << value << "\n";
    }

    cout << "Accessed: " << entry.accessed << ", Dirty: " << entry.dirty << "\n";
    cout << "-----------------------------\n";
totalAddresses++;
}

int main(int argc, char* argv[]) {
    srand(time(nullptr));

    if (argc < 2) {
        cerr << "Uso: ./virtual_memory_translate <endereço ou arquivo> [page_size]\n";
        return 1;
    }

    if (argc >= 3) {
        int argSize = stoi(argv[2]);
        if (argSize == 256 || argSize == 1024 || argSize == 2048 || argSize == 4096) {
            PAGE_SIZE = argSize;
        } else {
            cerr << "Tamanho de página inválido. Use 256, 1024, 2048 ou 4096.\n";
            return 1;
        }
    }




    TLB tlb;
    PageTableEntry* level1Table[PAGE_TABLE_ENTRIES_LVL1] = {nullptr};
    PageTableEntry pageTable16[PAGE_TABLE_ENTRIES_16] = {};

    string arg = argv[1];
    ifstream inputFile(arg);

    auto decide_mode_and_process = [&](unsigned int address) {
        if (address < (1 << 16)) {
            process_address_16bit(address, tlb, pageTable16);
        } else {
            process_address_32bit(address, tlb, level1Table);
        }
    };

    if (inputFile.is_open()) {
        string line;
        while (getline(inputFile, line)) {
            if (line.empty()) continue;
            unsigned int address = parse_address(line);
            if (address >= (1ULL << 32)) {
                cerr << "Endereço inválido: " << address << "\n";
                continue;
            }
            decide_mode_and_process(address);
        }
    } else {
        unsigned int address = parse_address(arg);
        decide_mode_and_process(address);
    }

    for (int i = 0; i < PAGE_TABLE_ENTRIES_LVL1; ++i) {
        delete[] level1Table[i];
    }

  ofstream report("relatorio_final.txt");
    if (report.is_open()) {
        report << "=========== RELATÓRIO FINAL ===========" << endl;
        report << "Total de endereços processados: " << totalAddresses << endl;
        report << "TLB hits: " << tlbHits << endl;
        report << "TLB misses: " << tlbMisses << endl;
        report << "Page hits: " << pageHits << endl;
        report << "Page faults: " << pageFaults << endl;
        report << "Dirty writes (páginas sujas escritas de volta): " << dirtyWrites << endl;
        report << "=======================================" << endl;
        report.close();
    } else {
        cerr << "Erro ao criar o arquivo relatorio_final.txt\n";
    }

    return 0;
}
