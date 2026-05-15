using System;
using System.Collections.Generic;
using libdb.Diagnostics;
using libdb.DB;
using System.Linq;

namespace EditorLib.IO
{
    public static class ObjectsSelector
    {
        public static int CollectObjects(
            IEditorCommonApplication application,
            string rootPath,
            Type typesMask,
            bool recursive,
            bool includeDeriven,
            out List<DBID> items
            )
        {
            Dictionary<Type, List<DBID>> result;
            var count = CollectObjects(
                application,
                rootPath,
                new Type[] { typesMask },
                recursive,
                includeDeriven,
                out result
                );

            try
            {
                items = new List<DBID>();
                foreach (var ids in result.Values)
                {
                    items.AddRange(ids);
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine(ex.ToString());
                throw;
            }

            return count;
        }

        public static int CollectObjects(
            IEditorCommonApplication application,
            string rootPath,
            Type[] typesMask,
            bool recursive,
            bool includeDeriven,
            out Dictionary<Type, List<DBID>> items
            )
        {
            if (ReferenceEquals(ObjectsRootFolder.Root, null))
            {
                ObjectsRootFolder.Initialize(application);
            }

            var folder = ObjectsRootFolder.Root.GetFolder(rootPath);
            items = new Dictionary<Type, List<DBID>>();

            if (!ReferenceEquals(folder, null))
            {
                var result = CollectObjects(folder, typesMask, recursive, includeDeriven, items);
                //Console.WriteLine("remove");
                RemoveDuplicates(items);
                return result;
            }
            else
            {
                Log.TraceError("Cannot collect types: root folder does not exist.");
                return 0;
            }
        }

        private static void RemoveDuplicates(Dictionary<Type, List<DBID>> items)
        {
            foreach (var type in new List<Type>(items.Keys))
            {
                Console.WriteLine("remove" + type);
                var ids = new Dictionary<DBID, DBID>();
                foreach (var dbId in items[type])
                {
                    Console.WriteLine("remove" + dbId);
                    if (!ids.ContainsKey(dbId))
                    {
                        ids.Add(dbId, dbId);
                    }
                }

                Console.WriteLine("____");
                Console.WriteLine(items[type].Count(x => x == null));
                items[type] = items[type].Where(x => x != null).ToList();
                //foreach (var dbId in items[type].Where(x => x != null))
                //{
                //Console.WriteLine(dbId);
                //}
                Console.WriteLine("____");

                Console.WriteLine("bla");
                items[type] = items[type].Distinct().ToList();// new List<DBID>(ids.Keys);
                Console.WriteLine("bla1");
            }
        }

        //class BoxEqualityComparer : IEqualityComparer<DBID>
        //{
        //    public bool Equals(DBID b1, DBID b2)
        //    {
        //        if (ReferenceEquals(b1, b2))
        //            return true;

        //        if (b2 is null || b1 is null)
        //            return false;

        //        return b1.Height == b2.Height
        //            && b1.Length == b2.Length
        //            && b1.Width == b2.Width;
        //    }

        //    public int GetHashCode(DBID box) => box.Height ^ box.Length ^ box.Width;

        //}

        private static int CollectObjects(ObjectsFolder folder, Type[] typesMask, bool recursive, bool includeDeriven, Dictionary<Type, List<DBID>> result)
        {
            int count = 0;
            foreach (var dbId in folder.Items)
            {
                //Console.WriteLine(dbId);
                count += ProcessResource(dbId, typesMask, includeDeriven, result);
                //Console.WriteLine("processed " + dbId);
            }

            //Console.WriteLine("recursive " + recursive);
            if (recursive)
            {
                foreach (var subfolder in folder.Folders)
                {
                    //Console.WriteLine(subfolder);
                    count += CollectObjects(subfolder, typesMask, recursive, includeDeriven, result);
                    //Console.WriteLine("processed " + subfolder);
                }
            }

            return count;
        }

        private static int ProcessResource(DBID dbId, Type[] typesMask, bool includeDeriven, Dictionary<Type, List<DBID>> result)
        {
            DBPtr<DBResource> res = DataBase.Get<DBResource>(dbId);

            if (res == null)
            {
                Log.TraceMessage("Resource \"{0}\" is not exist.", dbId.Name);
                return 0;
            }

            if (res.Get() == null)
            {
                Log.TraceMessage("Failed to load resource \"{0}\".", dbId.Name);
                return 0;
            }

            int count = 0;

            //Console.WriteLine($"I am {res.DBId}");

            foreach (var inlinedObjectId in res.GetLinks().Keys)
            {
                if (inlinedObjectId.IsInlined)
                {
                    count += ProcessResource(inlinedObjectId, typesMask, includeDeriven, result);
                }
            }
            //int i1 = 0;
            //Console.WriteLine(i1++);
            var dbObject = res.Get();
            //Console.WriteLine(i1++);

            foreach (var type in typesMask)
            {
                //Console.WriteLine(i1++);
                if (includeDeriven)
                {
                    //Console.WriteLine(i1++);
                    if (type.IsInstanceOfType(dbObject))
                    {
                        //Console.WriteLine("h" + i1++);
                        ++count;
                        AddResourceToResult(result, dbObject.GetType(), dbId);
                        //Console.WriteLine("h" + i1++);
                    }
                    //Console.WriteLine(i1++);
                }
                else
                {
                    //Console.WriteLine(i1++);
                    if (dbObject.GetType() == type)
                    {
                        //Console.WriteLine("h" + i1++);
                        ++count;
                        AddResourceToResult(result, type, dbId);
                        //Console.WriteLine("h" + i1++);
                    }
                    //Console.WriteLine(i1++);
                }
            }

            return count;
        }

        private static void AddResourceToResult(Dictionary<Type, List<DBID>> result, Type type, DBID dbId)
        {
            if (result.ContainsKey(type))
            {
                result[type].Add(dbId);
            }
            else
            {
                result.Add(type, new List<DBID>() { dbId });
            }
        }

        public delegate bool ProcessStructFunctor<T>(DBResource owner, T obj);

        public static bool ProcessStructs<T>(IEditorCommonApplication application, string path, bool recursive, ProcessStructFunctor<T> functor)
        {
            List<DBID> resDBIDs;

            CollectObjects(application, path, typeof(DBResource), recursive, true, out resDBIDs);

            FieldsWalker.DepthController depth = new FieldsWalker.DepthController(0, FieldsWalker.ObjectType.DBPtr, FieldsWalker.ObjectType.All);

            bool noInterruptions = true;

            foreach (DBID resDBID in resDBIDs)
            {
                DBResource res = DataBase.Get<DBResource>(resDBID).Get();

                if (null == res)
                    continue;

                FieldsWalker.VisitFields<T>(res, (ref T obj) => { noInterruptions = noInterruptions && functor(res, obj); return noInterruptions; }, depth.Functor);

                if (!noInterruptions)
                    break;
            }

            return noInterruptions;
        }

        public static bool ProcessStructs(IEditorCommonApplication application, string path, bool recursive, Function<bool, DBResource, object> functor)
        {
            List<DBID> resDBIDs;

            CollectObjects(application, path, typeof(DBResource), recursive, true, out resDBIDs);

            FieldsWalker.DepthController depth = new FieldsWalker.DepthController(0, FieldsWalker.ObjectType.DBPtr, FieldsWalker.ObjectType.All);

            bool noInterruptions = true;

            foreach (DBID resDBID in resDBIDs)
            {
                DBResource res = DataBase.Get<DBResource>(resDBID).Get();

                if (null == res)
                    continue;

                FieldsWalker.VisitFields(res, (ref object obj) =>
                {
                    noInterruptions = noInterruptions && functor(res, obj);
                    return noInterruptions;
                }, depth.Functor);

                if (!noInterruptions)
                    break;
            }

            return noInterruptions;
        }
    }
}
