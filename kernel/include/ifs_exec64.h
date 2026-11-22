#ifndef INTELLECT_IFS_EXEC64_H
#define INTELLECT_IFS_EXEC64_H
/* Load the signed and build-pinned bootstrap IVX from the verified IFS-1 root.
 * This intentionally restricted bootstrap does not implement dynamic imports,
 * general IVX verification, linking, relocations, or a trust-store update.
 * Return 1 only when the external terminal is ready for U1 Ring3 execution.
 */
int intellect_ifs_exec64_prepare(void);
#endif
