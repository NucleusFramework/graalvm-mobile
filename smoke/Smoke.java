import java.nio.file.Files;
import java.nio.file.Path;
import java.time.ZoneId;
import java.time.ZonedDateTime;
import java.util.Random;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import org.apache.lucene.analysis.standard.StandardAnalyzer;
import org.apache.lucene.document.Document;
import org.apache.lucene.document.Field;
import org.apache.lucene.document.IntPoint;
import org.apache.lucene.document.KnnByteVectorField;
import org.apache.lucene.document.StoredField;
import org.apache.lucene.document.TextField;
import org.apache.lucene.index.DirectoryReader;
import org.apache.lucene.index.IndexWriter;
import org.apache.lucene.index.IndexWriterConfig;
import org.apache.lucene.index.Term;
import org.apache.lucene.search.BooleanClause;
import org.apache.lucene.search.BooleanQuery;
import org.apache.lucene.search.IndexSearcher;
import org.apache.lucene.search.KnnByteVectorQuery;
import org.apache.lucene.search.PhraseQuery;
import org.apache.lucene.search.TermQuery;
import org.apache.lucene.search.TopDocs;
import org.apache.lucene.store.Directory;
import org.apache.lucene.store.NIOFSDirectory;
import org.apache.lucene.util.QueryBuilder;
import org.graalvm.nativeimage.IsolateThread;
import org.graalvm.nativeimage.c.function.CEntryPoint;
import org.graalvm.nativeimage.c.type.CCharPointer;
import org.graalvm.nativeimage.c.type.CTypeConversion;

/** The smoke test: a native image's JDK (files, threads, time zones) and Lucene (index, phrase, KNN) on the target. */
public final class Smoke {
    @CEntryPoint(name = "smoke_run")
    static int run(IsolateThread thread, CCharPointer dir) {
        return run(CTypeConversion.toJavaString(dir));
    }

    public static void main(String[] args) {
        System.exit(run(args[0]));
    }

    static void check(boolean condition, String what) {
        if (!condition) throw new AssertionError(what);
        System.out.println("smoke: " + what);
    }

    static int run(String dir) {
        try {
            System.out.println("smoke: " + System.getProperty("os.name") + " " + System.getProperty("os.arch") + ", " + System.getProperty("java.vm.name"));
            ZonedDateTime jerusalem = ZonedDateTime.now(ZoneId.of("Asia/Jerusalem"));
            check(jerusalem.getZone().getId().equals("Asia/Jerusalem"), "time zones");
            ExecutorService pool = Executors.newFixedThreadPool(4);
            long sum = 0;
            java.util.List<java.util.concurrent.Callable<Long>> tasks = java.util.List.of(() -> 1L, () -> 2L, () -> 3L, () -> 4L);
            for (Future<Long> f : pool.invokeAll(tasks)) sum += f.get();
            pool.shutdown();
            check(sum == 10, "threads");

            Path path = Files.createTempDirectory(Path.of(dir), "smoke-index");
            Random random = new Random(1);
            try (Directory directory = new NIOFSDirectory(path)) {
                try (IndexWriter writer = new IndexWriter(directory, new IndexWriterConfig(new StandardAnalyzer()))) {
                    String[] texts = {"בראשית ברא אלהים את השמים ואת הארץ", "ואהבת לרעך כמוך", "the quick brown fox jumps over the lazy dog"};
                    for (int i = 0; i < 300; i++) {
                        Document doc = new Document();
                        doc.add(new TextField("text", texts[i % texts.length], Field.Store.YES));
                        doc.add(new IntPoint("book", i % 7));
                        doc.add(new StoredField("id", i));
                        byte[] vector = new byte[64];
                        random.nextBytes(vector);
                        doc.add(new KnnByteVectorField("vec", vector));
                        writer.addDocument(doc);
                    }
                }
                try (DirectoryReader reader = DirectoryReader.open(directory)) {
                    IndexSearcher searcher = new IndexSearcher(reader);
                    TopDocs phrase = searcher.search(new QueryBuilder(new StandardAnalyzer()).createPhraseQuery("text", "ואהבת לרעך", 0), 10);
                    check(phrase.totalHits.value() == 100, "phrase query (" + phrase.totalHits + ")");
                    BooleanQuery bool = new BooleanQuery.Builder()
                        .add(new TermQuery(new Term("text", "fox")), BooleanClause.Occur.MUST)
                        .add(IntPoint.newExactQuery("book", 3), BooleanClause.Occur.FILTER)
                        .build();
                    check(searcher.count(bool) > 0, "boolean and point queries");
                    byte[] query = new byte[64];
                    random.nextBytes(query);
                    TopDocs knn = searcher.search(new KnnByteVectorQuery("vec", query, 5), 5);
                    check(knn.scoreDocs.length == 5, "KNN");
                    check(searcher.storedFields().document(knn.scoreDocs[0].doc).getField("id") != null, "stored fields");
                }
            }
            System.out.println("smoke ok");
            return 0;
        } catch (Throwable t) {
            t.printStackTrace();
            return 1;
        }
    }
}
