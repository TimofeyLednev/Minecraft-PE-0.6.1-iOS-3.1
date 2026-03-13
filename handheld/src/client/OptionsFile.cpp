#include "OptionsFile.h"
#include <stdio.h>
#include <string.h>

OptionsFile::OptionsFile() {
#ifdef __APPLE__
	settingsPath = "./Documents/options.txt";
#elif defined(ANDROID)
	settingsPath = "options.txt";
#else
	settingsPath = "options.txt";
#endif
}

void OptionsFile::setPath(const std::string& path) {
	settingsPath = path + "/options.txt";
}

void OptionsFile::save(const StringVector& settings) {
	FILE* pFile = fopen(settingsPath.c_str(), "w");
	if(pFile != NULL) {
		for(StringVector::const_iterator it = settings.begin(); it != settings.end(); ++it) {
			fprintf(pFile, "%s\n", it->c_str());
		}
		fclose(pFile);
	}
}

StringVector OptionsFile::getOptionStrings() {
	StringVector returnVector;
	FILE* pFile = fopen(settingsPath.c_str(), "r");
	if(pFile != NULL) {
		char lineBuff[128];
		while(fgets(lineBuff, sizeof lineBuff, pFile)) {
			// Strip trailing newline
			size_t len = strlen(lineBuff);
			if(len > 0 && lineBuff[len - 1] == '\n')
				lineBuff[len - 1] = '\0';
			std::string line(lineBuff);
			// Split on ':' into key and value
			size_t sep = line.find(':');
			if(sep != std::string::npos && sep + 1 < line.size()) {
				returnVector.push_back(line.substr(0, sep));
				returnVector.push_back(line.substr(sep + 1));
			}
		}
		fclose(pFile);
	}
	return returnVector;
}
