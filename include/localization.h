#ifndef LOCALIZATION_H
#define LOCALIZATION_H

void localizationInitialize(void);
const char *localizationGet(const char *key);
const char *localizationLanguage(void);
int localizationValidLanguageName(const char *name);

#define L(key) localizationGet(key)

#endif
