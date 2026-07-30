# RTG portable Transmog slot-state bridge

`mod-transmog` is authoritative for paper-doll slot state.

Portable root rows publish hidden `RTGTMOGSLOT` metadata containing:

- inventory slot
- whether an item-instance target is equipped
- target item entry
- currently displayed appearance entry
- displayed appearance quality

RTG_Core consumes this metadata only for presentation. It does not decide compatibility or infer empty targets from Scoreboard records.

Empty slots remain visible, but applying an appearance still requires an equipped target because the existing persistence model attaches a fake appearance to an item-instance GUID.
