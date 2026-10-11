#ifndef WINGS64_DC_SAVE_VMU_EEPROM_H
#define WINGS64_DC_SAVE_VMU_EEPROM_H

#define VMU_EEPROM_SIZE 0x800
/* Fill img from the VMU save if one exists. Returns 1 if loaded, 0 if no save/VMU (img zero-filled). */
int vmu_eeprom_load(unsigned char img[VMU_EEPROM_SIZE]);
/* Write the image. Returns 1 on success, 0 if no VMU or the write failed (e.g. full: needs ~6 blocks). */
int vmu_eeprom_save(const unsigned char img[VMU_EEPROM_SIZE]);
/* Short status for the screen/log: "VMU a1", "no VMU", "VMU full", ... */
const char *vmu_eeprom_status(void);

#endif /* WINGS64_DC_SAVE_VMU_EEPROM_H */
