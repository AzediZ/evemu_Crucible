# Missing type attributes

`gen_missing_type_attributes.py` writes `sql/migrations/20261011150000-type-attributes-missing.sql`: the dogma
attributes (and effects) of the types the database had no rows for at all, taken from the Crucible client's own
bulkdata.  Without them those types load with every attribute at its default, so e.g. mission npcs spawned with 0 hp.

```
python -I utils/type-attributes/gen_missing_type_attributes.py --attrs "<EVE client>/bulkdata/800006.cache2" --effects "<EVE client>/bulkdata/800007.cache2"
```

- 800006 is the client's `dgmTypeAttributes` (value, typeID, attributeID), 800007 its `dgmTypeEffects`
  (typeID, effectID, isDefault).  A csv of the decoded rows in the same column order works too.
- A type gets rows only when it is in `invTypes` and has no rows in that table in `sql/base` or in any other
  migration, so types that already have attributes keep them untouched.  Attributes and effects the database does
  not define are left out.
- Values go to `valueInt` or `valueFloat` the way the repo stores most rows of that attribute; non-integral values
  always go to `valueFloat`.
- The output is deterministic.  Change the generator rather than the generated SQL, run it again and commit both;
  on a database that already has the previous version, migrate it down, then up again.
