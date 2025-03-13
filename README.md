# Memmo

Memmo is a 1kLOC Memory scanner with no deps.
When I was a kid I loved playing pinball on windows xp.
After i discovered cheat engine I had the best score in the house :)

Roadmap
- Search for bytes (int32, int64, etc)
- Narrow search with multiple iterations
- Write bytes

// Find current mapped regions using maps
// Iterate through those regions and use process_vm_readv() to read them
// Use memchr to find portion of memory
// Store them and later perform multiple searches to restric them
// Allow the user to write them